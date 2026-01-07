# OmniLisp TODO

## Review Directive

**All newly implemented features must be marked with `[R]` (Review Needed) until explicitly approved by the user.**

- When an agent completes implementing a feature, mark it `[R]` (not `[DONE]`)
- `[R]` means: code is written and working, but awaits user review/approval
- After user approval, change `[R]` to `[DONE]`
- Workflow: `[TODO]` → implement → `[R]` → user approves → `[DONE]`

---

## STANDBY Notice
Phases marked with **[STANDBY]** are implementation-complete or architecturally stable but are currently deprioritized to focus on **Phase 17: Runtime Bridge & Feature Wiring**. This ensures the language is usable and "wired" before further optimizing the memory substrate.

---

## Phase 13: Region-Based Reference Counting (RC-G) Refactor

Replace hybrid memory management with a unified Region-RC architecture.

- [DONE] Label: T-rcg-arena
  Objective: Integrate `tsoding/arena` as the physical allocator backend.
- [DONE] Label: T-rcg-region
  Objective: Implement the Logical `Region` Control Block.
- [DONE] Label: T-rcg-ref
  Objective: Implement `RegionRef` fat pointer and atomic ops.
- [DONE] Label: T-rcg-transmigrate
  Objective: Implement Adaptive Transmigration (Deep Copy + Promotion).
- [DONE] Label: T-rcg-constructors
  Objective: Implement Region-Aware Value Constructors.
- [DONE] Label: T-rcg-cleanup
  Objective: Remove obsolete runtime components.

---

## Phase 14: ASAP Region Management (Static Lifetimes) [STANDBY]

**Objective:** Implement static liveness analysis to drive region deallocation, with RC as a fallback only.
**Reference:** `docs/STATIC_REGION_LIFETIME_ARCHITECTURE.md`

- [TODO] Label: T-asap-region-liveness
...
- [TODO] Label: T-asap-region-main
...

---

## Phase 15: Branch-Level Region Narrowing

**Objective:** Reduce RC overhead by keeping branch-local data out of RC-managed regions.
**Reference:** `docs/BRANCH_LEVEL_REGION_NARROWING.md`

- [TODO] Label: T1-analysis-scoped-escape
  Objective: Implement hierarchical, branch-level escape analysis to support "Region Narrowing", allowing temporary variables in non-escaping branches to be allocated on the stack or scratchpad instead of the parent region.
  Reference: docs/BRANCH_LEVEL_REGION_NARROWING.md
  Where: csrc/analysis/analysis.h, csrc/analysis/analysis.c
  Why:
    Currently, escape analysis (`EscapeClass`) is likely function-global. If a variable escapes *anywhere* in the function, it's marked as escaping.
    For "Region Narrowing", we need to know if a variable escapes *its specific branch* (e.g., the `then` block of an `if`).
    If `x` is created in `then` and only used there, it should be `ESCAPE_NONE` relative to that block, even if the function returns something else.
    This enables the compiler to emit `stack_alloc` or `scratch_free` at the end of that specific branch.
  
  What to change:
    1.  **Define Scope Hierarchy:** Introduce a way to track nested scopes (blocks) within a function.
    2.  **Scoped Escape Tracking:** Track variable escape status *per scope*.
    3.  **Escape Propagation:** Implement rules for how escape status bubbles up (e.g., escaping a child scope -> escaping to parent).

  Implementation Details:

    **Step 1: Extend Data Structures (in `analysis.h`)**
    Add a `ScopeID` or `BlockID` concept.
    
    ```c
    // New enum for finer-grained escape targets
    typedef enum {
        ESCAPE_TARGET_NONE = 0,    // Stays in current scope
        ESCAPE_TARGET_PARENT,      // Escapes to enclosing scope
        ESCAPE_TARGET_RETURN,      // Returns from function
        ESCAPE_TARGET_GLOBAL       // Stored globally
    } EscapeTarget;

    // Structure to track a variable's status within a specific scope
    typedef struct ScopedVarInfo {
        char* name;
        int scope_depth;           // Depth of the scope where defined
        EscapeTarget target;       // Where does it escape to?
        struct ScopedVarInfo* next;
    } ScopedVarInfo;

    // Extend AnalysisContext
    typedef struct AnalysisContext {
        // ... existing fields ...
        int current_scope_depth;
        ScopedVarInfo* scoped_vars; // Stack or list of var info
        // ...
    } AnalysisContext;
    ```

    **Step 2: Implement Scope Management (in `analysis.c`)**
    Create helper functions to enter/exit scopes.

    ```c
    void omni_scope_enter(AnalysisContext* ctx) {
        ctx->current_scope_depth++;
    }

    void omni_scope_exit(AnalysisContext* ctx) {
        // 1. Identify vars defined in this scope (ctx->current_scope_depth)
        // 2. If target == ESCAPE_TARGET_NONE, mark for local optimization (e.g., stack alloc)
        // 3. Remove them from the active tracking list (pop stack)
        ctx->current_scope_depth--;
    }
    ```

    **Step 3: Update `omni_analyze_escape`**
    Modify the main analysis loop to handle AST nodes that create scopes (`if`, `let`, `progn`/`block`).

    ```c
    void analyze_expr(AnalysisContext* ctx, OmniValue* expr) {
        // ...
        if (is_if_node(expr)) {
            // Analyze Condition
            analyze(ctx, expr->cond);
            
            // Analyze Then Branch
            omni_scope_enter(ctx);
            analyze(ctx, expr->then_branch);
            omni_scope_exit(ctx); // <--- Decision point for narrowing
            
            // Analyze Else Branch
            if (expr->else_branch) {
                omni_scope_enter(ctx);
                analyze(ctx, expr->else_branch);
                omni_scope_exit(ctx);
            }
            return;
        }
        // ...
    }
    ```

  How to verify:
    1.  Create a test case `test_narrowing.c` that constructs a manual AST with nested scopes:
        ```lisp
        (defn test []
          (if true
            (let [x (list 1 2)]  ; x defined here
              (print x))         ; x used here, does NOT escape branch
            (return 1)))
        ```
    2.  Run the analysis.
    3.  Verify that `x` is marked as `ESCAPE_TARGET_NONE` (or equivalent) for its specific scope depth.
    4.  Verify that if you change `(print x)` to `(return x)`, the analysis correctly updates `x` to `ESCAPE_TARGET_RETURN`.

  Acceptance:
    - `AnalysisContext` tracks scope depth.
    - Variables correctly identify their defining scope.
    - Variables that do not leave a branch are flagged as non-escaping *for that branch*.
    - Variables that leave a branch (e.g. via return or assignment to outer var) are flagged as escaping.
- [TODO] Label: T2-codegen-narrowing
  Objective: Update code generation to utilize scoped escape analysis, emitting stack/scratch allocations for non-escaping variables and inserting precise cleanup at branch exits.
  Reference: docs/BRANCH_LEVEL_REGION_NARROWING.md
  Where: csrc/codegen/codegen.c, csrc/codegen/cleanup.c, csrc/analysis/analysis.h
  Why:
    Once we know a variable `x` doesn't escape its branch (T1), we must ensure:
    1. It is allocated cheaply (stack/scratch) instead of in the parent region.
    2. Its cleanup (if any) happens exactly when the branch exits.
    This realizes the memory savings of "Region Narrowing".
  
  What to change:
    1.  **Allocation Routing:** In `emit_alloc`, check `omni_get_escape_target(ctx, var)`.
        *   If `ESCAPE_TARGET_NONE`: Use `emit_stack_alloc` or `emit_scratch_alloc`.
        *   Else: Use `emit_region_alloc` (parent/global).
    2.  **Branch Cleanup:** In `emit_block_exit`, look up variables defined in this scope.
        *   If `ESCAPE_TARGET_NONE`: Emit `free_tree` (for stack) or do nothing (for scratch/stack scalars).

  Implementation Details:
    *   **Codegen Logic (`csrc/codegen/codegen.c`):**
        ```c
        void emit_alloc(CodegenContext* ctx, char* var, Type* type) {
            EscapeTarget target = omni_get_escape_target(ctx->analysis, var);
            if (target == ESCAPE_TARGET_NONE) {
                // Emit alloca or simple stack var
                fprintf(ctx->out, "%s %s;\n", type_to_c(type), var); 
            } else {
                // Emit region allocation
                fprintf(ctx->out, "%s %s = region_alloc(ctx->region, sizeof(%s));\n", 
                        type_to_c(type), var, type_to_c(type));
            }
        }
        ```
    *   **Cleanup Logic (`csrc/codegen/cleanup.c`):**
        ```c
        void emit_scope_exit(CodegenContext* ctx, int depth) {
            // Iterate over vars in this scope
            for (Var* v : ctx->scopes[depth].vars) {
                 if (v->escape_target == ESCAPE_TARGET_NONE && v->needs_cleanup) {
                     fprintf(ctx->out, "free_tree(%s);\n", v->name);
                 }
            }
        }
        ```

  Verification:
    *   Input: `(if true (let [x (cons 1 2)] x) 0)` 
        *   Output C: `Cell* x = alloca(sizeof(Cell)); ... free_tree(x);` (inside the `if` block).
    *   Input: `(let [x (cons 1 2)] (return x))`
        *   Output C: `Cell* x = region_alloc(r, ...); return x;`

  Acceptance:
    - Code generator queries escape analysis before emitting allocs.
    - Non-escaping variables result in stack/alloca C code.
    - Scope exit points (end of `if`, `let`) contain specific cleanup for locals.

---

## Phase 15b: Adaptive Region Sizing [STANDBY]

**Objective:** Scale region overhead with actual data size to reduce penalty for small lifecycle groups.
**Reference:** `docs/BRANCH_LEVEL_REGION_NARROWING.md` (Section 12)

- [TODO] Label: T-adaptive-size-classes
...
- [TODO] Label: T-adaptive-non-atomic-tiny
...

---

## Phase 16: Advanced Region Optimization [STANDBY]

**Objective:** Implement high-performance transmigration and tethering algorithms.
**Reference:** `docs/ADVANCED_REGION_ALGORITHMS.md`

- [DONE] Label: T-opt-bitmap-cycle
...
- [DONE] Label: T-opt-tether-cache
...

---

## Phase 17: Runtime Bridge & Feature Wiring

**Objective:** Complete the wiring of core language features into the evaluator and environment.

- [TODO] Label: T-wire-multiple-dispatch
  Objective: Implement robust Multiple Dispatch selection logic.
  Where: `src/runtime/eval/omni_eval.c`, `omni_apply`
  What to change:
    - [ ] Update `env_define` behavior to automatically convert `T_LAMBDA` to `T_GENERIC` on redefinition.
    - [ ] Implement Julia-style specificity sorting for methods (more specific Kinds before more general ones).
  How to verify: Run `tests/unwired_features.omni`; verify correct method selected for different Kinds.
  Acceptance:
    - Redefining a function with different types adds to the method table instead of overwriting.
    - Dispatch selects the most specific match.

- [TODO] Label: T-wire-parametric-subtyping
  Objective: Implement subtyping logic for parametric Kinds.
  Where: `src/runtime/eval/omni_eval.c`, `is_subtype`
  What to change:
    - [ ] Implement recursive comparison of Kind parameters in `is_subtype`.
    - [ ] Support covariance for immutable structures (e.g., `{List Int} <: {List Any}`).
  How to verify: `(type? {List Int} {List Any})` returns `true`.
  Acceptance:
    - Parametric types correctly identified in the subtype hierarchy.

- [TODO] Label: T-wire-pika-api
  Objective: Bridge Pika Grammar engine to Lisp environment.
  Where: `src/runtime/eval/omni_eval.c`
  What to change:
    - [ ] Register `pika-match` primitive (takes grammar, rule-name, string).
    - [ ] Register `pika-find-all` primitive.
  How to verify: Run `tests/unwired_features.omni`; verify grammar definition can be used to parse.
  Acceptance:
    - Pika engine functions accessible from Lisp.

- [TODO] Label: T-wire-deep-put
  Objective: Support recursive path mutation in `put!`.
  Where: `src/runtime/eval/omni_eval.c`, `eval_put_bang`
  What to change:
    - [ ] Rewrite `eval_put_bang` to traverse deep paths (`a.b.c`) and perform mutation at the leaf.
  How to verify: Mutate a nested dict field and verify the update reflects in the parent.
  Acceptance:
    - Deep path mutation works for both Arrays and Dicts.

- [TODO] Label: T-wire-iter-ext
  Objective: Implement `iterate` and `take` for infinite sequence support.
  Where: `src/runtime/eval/omni_eval.c`
  What to change:
    - [ ] Implement `prim_iterate` (lazy generator from function + seed).
    - [ ] Implement `prim_take` (lazy limiter for iterators).
  How to verify: `(collect-list (take 5 (iterate inc 0)))` returns `(0 1 2 3 4)`.
  Acceptance:
    - System supports infinite lazy sequences.

- [TODO] Label: T-wire-reader-macros
  Objective: Implement Sign `#` dispatch for literals.
  Where: `src/runtime/reader/omni_reader.c`
  What to change:
    - [ ] Add support for named characters: `#\newline`, `#\space`, `#\tab`.
    - [ ] Add support for `#fmt` interpolated strings.
  How to verify: Read `#\newline` and verify it produces character code 10.
  Acceptance:
    - Authoritative reader support for all Sign-prefixed literals.

- [TODO] Label: T-wire-modules
  Objective: Implement basic Module isolation.
  Where: `src/runtime/eval/omni_eval.c`, `eval_module`, `eval_import`
  What to change:
    - [ ] Wire `module` to create a fresh environment.
    - [ ] Wire `import` to selectively map symbols between environments.
  How to verify: Define a module, export a function, and import it into another scope.
  Acceptance:
    - Functional module system with namespace isolation.








