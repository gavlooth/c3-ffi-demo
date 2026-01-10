# Syntax & Implementation Inconsistencies

This document tracks identified inconsistencies in the language syntax, documentation, and implementation.

## 1. Parser Implementation Divergence

*   **Compiler Parser:** Located in `csrc/parser/parser.c`. Uses a Pika packrat parser.
*   **Runtime Parser:** Located in `src/runtime/pika/omni_grammar.c`. Seemingly a separate implementation.
*   **Risk:** The compiler and runtime may parse code differently, leading to "works in REPL, fails in compiler" bugs.
*   **Action:** Verify if `csrc/parser` and `runtime/src/pika` share the same grammar rules. Ideally, there should be a single source of truth for the grammar.

## 2. "Truthiness" Semantics

*   **Current Behavior (canonical):** `0` and `()` (empty list) are **truthy**. Only `false` and `nothing` are falsy.
*   **Documentation:** `SYNTAX.md` and `language_reference.md` confirm this.
*   **Inconsistency:** This deviates from standard Lisp dialects:
    *   **Scheme:** `#f` is the only false value. `()` is true.
    *   **Common Lisp:** `()` and `nil` are false.
    *   **C/C++:** `0` is false.
    *   **Python/JS:** `0`, `[]`, `""` are false.
*   **Action:** Confirm if this is a deliberate design choice or an artifact of the C implementation (`false` and `nothing` map to specific tagged pointers).

## 3. Implemented vs. Design Target

*   **Documentation:** `SYNTAX.md` lists high-level features (arrays, dicts, file I/O) as "Built-ins Wired Today". `LANGUAGE_REFERENCE.md` claims only a small core (`define`, `if`, etc.) is implemented in the C compiler.
*   **Reality:** `parser.c` shows support for arrays (`act_array`) but not dicts (`#{}`). The runtime likely supports more than the compiler.
*   **Action:** Update `LANGUAGE_REFERENCE.md` to accurately reflect the "Implemented Subset" for both the Compiler and the Runtime.

## 4. Bracket Usage & Syntax Support

*   **Documentation:** Claims support for:
    *   List-style `let`: `(let ((x 1)) ...)`
    *   Vector-style `let`: `(let [x 1] ...)`
    *   Array literals: `[1 2 3]`
    *   Dict literals: `#{:a 1}`
*   **Parser (`csrc/parser/parser.c`):**
    *   Has rules for `R_LBRACKET` (`[`) and `R_RBRACKET` (`]`).
    *   `act_array` implements `[ ... ]` syntax.
    *   **Missing:** No obvious rule or action for `#{ ... }` (Dict literals).
    *   **Missing:** `act_let` logic isn't in the parser file (likely handled in AST/Analysis phase), so support for vector-style binding needs verification in `ast.c` or `analysis.c`.

## 5. Lambda Aliases

*   **Documentation:** Lists `lambda`, `fn`, and `λ`.
*   **Implementation:** The parser (`act_sym`) doesn't seem to canonicalize these. The AST or Compiler likely checks for the symbol name "lambda".
*   **Risk:** If the compiler only checks for `strcmp(s, "lambda")`, then `fn` and `λ` won't work as expected unless they are macros or explicitly handled.

## 6. Quote vs Quasiquote

*   **Parser:** `act_quoted` handles `'`, `` ` ``, and `,`.
*   **Status:** Looks correctly implemented in the parser.

## 7. Plan

1.  **Unified Grammar:** Refactor to share grammar definitions between Compiler and Runtime if possible.
2.  **Verify Truthiness:** Add a test case to explicitly confirm/reject the `0` is true behavior.
3.  **Audit Built-ins:** Create a "Compiler Support Matrix" vs "Runtime Support Matrix".
4.  **Fix Dict Syntax:** Add `#{}` parsing to `csrc/parser/parser.c`.
5.  **Standardize Aliases:** Ensure `fn` and `λ` are treated as `lambda` in the compiler.
