# LLM Review Summary

Based on the analysis of the unattended LLM session (`ouput.save`), the following tasks and improvements have been identified.

## 1. Tasks to Revisit

### T-opt-transmigrate-lazy (Lazy Transmigration)
- **Status:** Currently TODO
- **LLM Reasoning:** The LLM raised a critical concern that lazy transmigration conflicts with the ASAP (As Static As Possible) memory management model.
    - **Conflict:** ASAP relies on immediate region deallocation. Lazy access to unmigrated objects in a freed source region would cause segfaults.
    - **Complexity:** Implementing this would require major refactoring (proxy objects, forwarding pointers) and changing the ABI for object access.
    - **Performance:** Benchmarks for "Batched Transmigration" (already DONE) showed it achieves equivalent performance to standard transmigration (0.95x-1.06x), making lazy optimization largely redundant.
- **Recommendation:** **Mark as N/A** in `TODO.md` with the reason: "Lazy transmigration conflicts with ASAP's immediate deallocation model and requires changing the object access ABI. Batched transmigration (DONE) already achieves equivalent performance."

### T-codegen-float-01 (Float Support)
- **Status:** Marked as [R] (Implemented)
- **Discrepancy:** While the *codegen* for floats was fixed to use `mk_float_region` and float primitives, the LLM noted a **separate parser issue**.
- **Issue:** Float literals like `1.5` are currently parsed incorrectly as path expressions (`1` accessing field `5`) instead of `OMNI_FLOAT` literals.
- **Action:** Create a new task to fix float literal parsing in `csrc/parser/parser.c`.

### T-opt-compiler-benchmark-typed-codegen
- **Status:** Marked as [R] (Implemented)
- **Discrepancy:** The benchmark results showed a **performance regression for Symbols**.
    - **Result:** Symbol allocation is 0.68x slower (35.10 ns -> 51.62 ns).
    - **Reason:** Suspected `strdup()` overhead in the new typed allocation path.
- **Action:** Investigate and optimize Symbol allocation in the new typed codegen path.

## 2. Completed Tasks (Verification)

The following tasks were successfully implemented and marked as `[R]` or `[DONE]` by the LLM. They should be reviewed for final approval:

*   **Pika Parser / Regex:**
    *   `T-wire-pika-exec-01`: `omni_pika_match` (convenience function) - **DONE**
    *   `T-wire-pika-exec-02`: `pika_extract_captures` (capture groups) - **DONE**
    *   `T-wire-pika-exec-04`: `prim_match_pattern` (runtime regex-like match) - **DONE**
*   **Codegen & Syntax:**
    *   `T-codegen-array-01`: Array literals `[1 2 3]` - **DONE**
    *   `T-codegen-params-01`: Function parameter limit increased (4 -> 16) - **DONE**
    *   `T-wire-char-literal-01`: Character literals `#\newline`, `#\x41` - **DONE**
    *   `T-wire-fmt-string-01`: Format strings `#fmt"Hello $name"` - **DONE**
    *   `T-wire-string-literal-03`: String equality (`strcmp`) - **DONE**
    *   `T-wire-type-objects-03`: Wired `type?` and type symbols - **DONE**

## 3. General Improvements

*   **Parser Robustness:** The issue with float parsing suggests a need for a broader review of literal parsing rules to prevent ambiguity with dot-syntax (path expressions).
*   **Symbol Performance:** The regression in symbol allocation suggests that `mk_symbol` might be doing redundant work (e.g., extra copying) in the new typed allocation model compared to the old one.
