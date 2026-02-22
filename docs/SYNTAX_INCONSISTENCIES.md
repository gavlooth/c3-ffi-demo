# Syntax & Implementation Inconsistencies

This document tracks identified inconsistencies in the language syntax, documentation, and implementation.

> **Last Updated:** 2026-01-14

## 1. Parser Implementation Divergence

*   **Status:** ✅ **RESOLVED** (2026-01-14)
*   **Compiler Parser:** Located in `csrc/parser/parser.c`. Uses a Omni packrat parser.
*   **Runtime Parser:** The file `src/runtime/omni/omni_grammar.c` **does not exist**.
*   **Resolution:** There is only ONE parser - the Omni-based parser in `csrc/parser/`.
    The runtime does not have a separate parser; it executes compiled output.
*   **Single Source of Truth:** `csrc/parser/parser.c` is the authoritative grammar.

## 2. "Truthiness" Semantics

*   **Status:** ✅ **DOCUMENTED** (deliberate design choice)
*   **Current Behavior (canonical):** `0` and `()` (empty list) are **truthy**. Only `false` and `nothing` are falsy.
*   **Documentation:** `SYNTAX.md` and `language_reference.md` confirm this.
*   **Design Rationale:** This is a deliberate design choice:
    *   Avoids C's confusing "0 is false" semantics
    *   Empty list `()` is truthy (unlike Common Lisp's nil)
    *   Only explicit `false` and `nothing` are falsy
*   **Implementation:** `false` and `nothing` are recognized in `parser.c:158-159` and
    handled as special symbols in the analyzer.

## 3. Implemented vs. Design Target

*   **Status:** ⚠️ **PARTIALLY RESOLVED** (2026-01-14)
*   **Parser Implementation:**
    *   ✅ Arrays `[...]` - implemented (`act_array`)
    *   ✅ Dicts `#{}` - **IMPLEMENTED** (2026-01-14)
    *   ✅ Signed integers `+456` - **IMPLEMENTED** (2026-01-14)
    *   ❌ Partial floats `.5`, `3.` - **NOT implemented**
*   **Resolution:** `SYNTAX.md` now has an "Implementation Status" section documenting
    what is actually implemented vs designed.
*   **Action:** See TODO.md for implementation tasks.

## 4. Bracket Usage & Syntax Support

*   **Status:** ⚠️ **PARTIALLY IMPLEMENTED** (2026-01-14)
*   **Parser (`csrc/parser/parser.c`):**
    *   ✅ `R_LBRACKET`/`R_RBRACKET` - defined
    *   ✅ `act_array` - implements `[...]` syntax
    *   ✅ `R_HASHBRACE` terminal defined but **unused**
    *   ❌ Dict literals `#{...}` - **NO parser rule or action**
*   **Analyzer (`csrc/analysis/analysis.c`):**
    *   ✅ `let` with bracket bindings handled in analyzer
    *   Binding forms like `(let [x 1] ...)` are analyzed, not parsed specially

## 5. Lambda Aliases

*   **Status:** ✅ **RESOLVED** (2026-01-14)
*   **Documentation:** Lists `lambda`, `fn`, and `λ`.
*   **Implementation:**
    *   Parser outputs these as plain symbols (no canonicalization)
    *   Analyzer recognizes all three: `analysis.c:1170-1171`
    *   Codegen handles all three: `codegen.c:2853-2854`
*   **Verification:** `strcmp(name, "lambda") == 0 || strcmp(name, "fn") == 0 || strcmp(name, "λ") == 0`

## 6. Quote vs Quasiquote

*   **Status:** ✅ **IMPLEMENTED**
*   **Parser:** `act_quoted` handles `'`, `` ` ``, `,`, and `,@`:
    *   `'expr` → `(quote expr)`
    *   `` `expr `` → `(quasiquote expr)`
    *   `,expr` → `(unquote expr)`
    *   `,@expr` → `(unquote-splicing expr)`

## 7. Remaining Work

1.  ✅ **Unified Grammar:** RESOLVED - only one parser exists (`csrc/parser/parser.c`)
2.  ✅ **Truthiness:** DOCUMENTED - deliberate design choice
3.  ⚠️ **Implementation Status:** Added to `SYNTAX.md` - see Implementation Status section
4.  ❌ **Dict Syntax:** Add `#{}` parsing to `csrc/parser/parser.c` - see TODO.md
5.  ✅ **Lambda Aliases:** RESOLVED - `fn` and `λ` handled in analyzer

### New Tasks (Added 2026-01-14)

See `TODO.md` for implementation tasks:
- ✅ **Implement dict literal parsing `#{}`**
- ✅ **Implement signed integer parsing `+456`**
- Implement partial float parsing `.5`, `3.`
- Complete match clause parsing
