# OmniLisp TODO

## Review Directive

**All newly implemented features must be marked with `[R]` (Review Needed) until explicitly approved by the user.**

- When an agent completes implementing a feature, mark it `[R]` (not `[DONE]`)
- `[R]` means: code is written and working, but awaits user review/approval
- After user approval, change `[R]` to `[DONE]`
- Workflow: `[TODO]` → implement → `[R]` → user approves → `[DONE]`

This ensures no feature is considered "done" until a human has reviewed it.

---

## Phase 10: Grammar DSL for Full Pika Power

Replace regex-based pattern matching with a native OmniLisp grammar DSL that exposes Pika's full capabilities including left recursion, grammar composition, and named rules.

### Motivation

The current regex-to-PikaClause approach is a **lossy translation**:

| Pika Capability | Regex | Current Status |
|----------------|-------|----------------|
| Left recursion (`expr <- expr '+' term`) | ❌ Impossible | ❌ Not accessible |
| Named rules / composition | ❌ No | ❌ Not accessible |
| Indirect recursion | ❌ No | ❌ Not accessible |
| Substring matching | ✅ Implicit | ✅ Works |
| All matches | ❌ First only | ✅ Via `find_all` |

### Design: `[grammar]` Form

Following OmniLisp's established `[syntax name]` and `[method name Type]` patterns:

```lisp
;; Grammar definition - creates a grammar VALUE (not a type)
(define [grammar arithmetic]
  [expr   (first (seq (ref expr) "+" (ref term))    ; LEFT RECURSION WORKS!
                 (seq (ref expr) "-" (ref term))
                 (ref term))]
  [term   (first (seq (ref term) "*" (ref factor))
                 (seq (ref term) "/" (ref factor))
                 (ref factor))]
  [factor (first (seq "(" (ref expr) ")")
                 (one-or-more (charset "0-9")))])

;; Usage
(pika-match arithmetic :expr "1+2*3")
(pika-find-all arithmetic :expr source-code)
```

### Clause Primitives

```lisp
;; Terminals
"literal"                    ; Exact string match
(char #\x)                   ; Single character
(charset "a-zA-Z0-9_")       ; Character class (Pika pattern syntax)
(charset-not "0-9")          ; Negated character class
(any)                        ; Any single character

;; Combinators
(seq a b c ...)              ; Sequence (all must match in order)
(first a b c ...)            ; Ordered choice (first match wins)
(ref rule-name)              ; Reference another rule (enables recursion)

;; Quantifiers
(zero-or-more x)             ; x* (zero or more)
(one-or-more x)              ; x+ (one or more)
(optional x)                 ; x? (zero or one)

;; Lookahead (PEG predicates)
(followed-by x)              ; &x - positive lookahead (match without consuming)
(not-followed-by x)          ; !x - negative lookahead

;; AST labeling
(label :name x)              ; Tag match for AST construction
```

### Example: JSON Grammar

```lisp
(define [grammar json]
  [ws      (zero-or-more (charset " \\t\\n\\r"))]

  [value   (seq (ref ws)
               (first (ref object) (ref array) (ref string)
                      (ref number) (ref bool) (ref null))
               (ref ws))]

  [object  (label :object
             (seq "{" (ref ws) (optional (ref pairs)) "}"))]

  [pairs   (seq (ref pair)
               (zero-or-more (seq "," (ref pair))))]

  [pair    (seq (ref string) (ref ws) ":" (ref value))]

  [array   (label :array
             (seq "[" (ref ws) (optional (ref elements)) "]"))]

  [elements (seq (ref value)
                (zero-or-more (seq "," (ref value))))]

  [string  (label :string
             (seq "\""
                  (zero-or-more (first (ref escape) (charset-not "\"\\\\")))
                  "\""))]

  [escape  (seq "\\\\" (charset "\"\\\\/bfnrt"))]

  [number  (label :number
             (seq (optional "-")
                  (first "0" (seq (charset "1-9")
                                  (zero-or-more (charset "0-9"))))
                  (optional (seq "." (one-or-more (charset "0-9"))))))]

  [bool    (label :bool (first "true" "false"))]
  [null    (label :null "null")])
```

### Tasks

- [ ] **T-grammar-dsl-design**: Finalize grammar DSL syntax
  - **Objective**: Approve `[grammar name]` form and clause primitives
  - **Where**: Design document (this file)
  - **What to change**: Review and approve the proposed syntax above
  - **Acceptance**:
    - `[grammar name]` form approved
    - Clause primitives list finalized
    - Rule syntax `[name clause]` approved

- [ ] **T-grammar-dsl-reader**: Implement reader support for `[grammar ...]` in define
  - **Objective**: Parser recognizes `(define [grammar name] ...)` form
  - **Where**: `src/runtime/pika/pika_reader.c` or equivalent
  - **What to change**:
    - Add special case in define parsing for `[grammar name]`
    - Parse rule bodies as `[name clause]` pairs
  - **How to verify**: `(define [grammar test] [rule (seq "a" "b")])` parses without error
  - **Acceptance**:
    - Reader accepts grammar definitions
    - Syntax errors reported with line/column

- [ ] **T-grammar-dsl-compiler**: Implement grammar-to-PikaClause compiler
  - **Objective**: Convert parsed grammar AST to PikaGrammar
  - **Where**: `src/runtime/pika/omni_grammar.c`
  - **What to change**:
    - Add `compile_grammar_dsl()` function
    - Convert each clause form to corresponding `pika_clause_*` call
    - Build rule array and create `PikaGrammar`
  - **How to verify**: Compile simple grammar and match against input
  - **Acceptance**:
    - All clause primitives supported
    - Left recursion works
    - Named rules reference each other correctly

- [ ] **T-grammar-dsl-runtime**: Implement runtime API
  - **Objective**: Add `pika-match` and `pika-find-all` primitives
  - **Where**: `src/runtime/tower/tower.c` (primitives)
  - **What to change**:
    - Add `pika-match` primitive: `(pika-match grammar :rule input)`
    - Add `pika-find-all` primitive: `(pika-find-all grammar :rule input)`
  - **How to verify**: Integration test with arithmetic grammar
  - **Acceptance**:
    - Both primitives work with compiled grammars
    - Returns Value* results consistent with existing API

- [ ] **T-grammar-dsl-tests**: Add comprehensive tests
  - **Objective**: Test all grammar DSL features
  - **Where**: `tests/test_pika_tower.c`
  - **What to change**:
    - Test left recursion (Pika's killer feature)
    - Test indirect recursion
    - Test all clause primitives
    - Test error handling for malformed grammars
  - **How to verify**: `make && ./test_pika_tower`
  - **Acceptance**:
    - Left recursion test passes
    - All primitives tested
    - Error messages are helpful

- [ ] **T-grammar-dsl-docs**: Document grammar DSL
  - **Objective**: User documentation for grammar DSL
  - **Where**: `docs/GRAMMAR_DSL.md` (new file)
  - **What to change**:
    - Full syntax reference
    - Examples for common use cases
    - Comparison with regex patterns
    - Pika-specific features explained
  - **Acceptance**:
    - Documentation is complete and accurate

---

## Phase 11: Fix/Replace Broken PEG Parser [N/A]

**Status: N/A** - The grammar DSL (Phase 10) makes text-based PEG parsing unnecessary.

### Rationale

The `pika_meta_parse()` function is broken, but fixing it provides no value over the DSL:
- DSL uses the Lisp reader (already working)
- DSL is more composable (it's just Lisp)
- DSL has better error messages
- DSL is more "OmniLisp native"

The only use case for PEG text would be copy-pasting grammars from external sources, which is not a priority.

### Deprecated Tasks

- [N/A] **T-fix-peg-diagnose**: Diagnose `pika_meta_parse()` failure
  - **Reason**: DSL makes this unnecessary

- [N/A] **T-fix-peg-repair**: Fix or replace `pika_meta_parse()`
  - **Reason**: DSL makes this unnecessary

- [N/A] **T-fix-peg-tests**: Add regression tests for PEG parsing
  - **Reason**: DSL makes this unnecessary

### Future Consideration

If PEG text compatibility is ever needed, implement it as a DSL macro that parses PEG syntax at compile time:
```lisp
(define arithmetic (peg-grammar "
  expr <- expr '+' term / term
  term <- [0-9]+
"))
```
This would use the DSL infrastructure internally.

---

## Pikaparser Pattern Compiler Upgrade ✅ COMPLETED

Upgrade `omni_compile_pattern()` to support extended regex syntax while preserving Pika's full power.

## Implementation Architecture

```
┌─────────────────────────────────────────────────────────────────────────────┐
│                           USER API LAYER                                    │
├─────────────────────────────────────────────────────────────────────────────┤
│                                                                              │
│  ┌─────────────────────────────┐    ┌─────────────────────────────────┐  │
│  │   Simple Regex Patterns      │    │    Direct Pika Clause API      │  │
│  │   (NEW - implemented)        │    │    (EXISTING - unchanged)      │  │
│  │                             │    │                                 │  │
│  │  omni_pika_match("[a-z]+")   │    │  pika_clause_seq(...)          │  │
│  │  omni_pika_match("foo|bar")  │    │  pika_clause_first(...)        │  │
│  │  omni_pika_match("\\d+")     │    │  pika_clause_one_or_more(...)   │  │
│  └─────────────────────────────┘    └─────────────────────────────────┘  │
│              │                                 │                          │
│              ▼                                 ▼                          │
│  ┌───────────────────────────┐    ┌─────────────────────────────────┐  │
│  │  Pattern Compiler         │    │  Manual Grammar Construction   │  │
│  │  omni_compile_pattern()   │    │  (for advanced use)            │  │
│  └───────────────────────────┘    └─────────────────────────────────┘  │
│              │                                 │                          │
│              ▼                                 │                          │
└──────────────────┬────────────────────────────────┼──────────────────────────┘
                   │                                │
                   ▼                                ▼
┌─────────────────────────────────────────────────────────────────────────────┐
│                        PATTERN COMPILER PIPELINE                             │
│                        (NEW CODE - omni_grammar.c)                         │
├─────────────────────────────────────────────────────────────────────────────┤
│                                                                              │
│  Regex Pattern → Tokens → AST → PikaClauses → Grammar                      │
│                  [Lexer] [Parser]   [Codegen]   [Builder]                   │
│                                                                              │
│  1. LEXER (regex_tokenize)                                                  │
│     RTOK_CHAR, RTOK_CHARSET, RTOK_ESCAPE, RTOK_STAR, etc.                   │
│                                                                              │
│  2. PARSER (parse_pattern)                                                  │
│     Precedence: atom < quantifier < sequence < alternation                  │
│                                                                              │
│  3. CODEGEN (ast_to_clause)                                                 │
│     Direct conversion to PikaClause structures                              │
│     (Bypasses broken pika_meta_parse - see note below)                      │
│                                                                              │
└──────────────────┬──────────────────────────────────────────────────────────┘
                   │
                   ▼
┌─────────────────────────────────────────────────────────────────────────────┐
│                        PIKA PARSER CORE (pika_c/pika.c)                       │
│                        (UNCHANGED - all power preserved)                     │
├─────────────────────────────────────────────────────────────────────────────┤
│                                                                              │
│  ╔═══════════════════════════════════════════════════════════════════════╗  │
│  ║  PIKA UNIQUE CAPABILITIES (all still available)                        ║  │
│  ╠═══════════════════════════════════════════════════════════════════════╣  │
│  ║  ✅ Left recursion support      expr <- expr '+' term                 ║  │
│  ║  ✅ Substring matching           Finds matches anywhere in input     ║  │
│  ║  ✅ All non-overlapping matches find_all returns every match        ║  │
│  ║  ✅ Grammar composition          Build complex parsers from rules    ║  │
│  ║  ✅ Bottom-up DP with memo      O(n³) worst case, O(n) typical      ║  │
│  ╚═══════════════════════════════════════════════════════════════════════╝  │
│                                                                              │
└─────────────────────────────────────────────────────────────────────────────┘
```

## Important Note: Broken Function

**`pika_meta_parse()` is broken** - The meta-parser in `pika_c/pika.c` has a bug that causes
it to reject valid PEG grammars. For example:

```c
pika_meta_parse("pattern <- [0-9]+\n", &error);
// Returns: "Syntax errors in grammar spec [7,9]  <"
// Positions 7-9 are "<-" which should be valid!
```

**Workaround**: The new implementation bypasses `pika_meta_parse()` entirely by converting
AST directly to `PikaClause` structures. This is actually more efficient and gives better
control over the generated grammar.

**Impact**:
- ✅ `omni_compile_pattern()` - Works with new implementation
- ❌ `omni_compile_peg()` - Still broken (relies on pika_meta_parse)
- ✅ Direct clause construction - Fully functional

## Parsing Power Hierarchy (Preserved)

```
┌─────────────────────────────────────────────────────────────────┐
│                    PARSING POWER HIERARCHY                       │
├─────────────────────────────────────────────────────────────────┤
│                                                                  │
│  Formal Regex (DFA)                                              │
│      │  • No nesting, no recursion                               │
│      │  • O(n) matching                                          │
│      ▼                                                           │
│  Standard PEG (Packrat)                                          │
│      │  • Context-free grammars                                  │
│      │  • NO left recursion (infinite loop)                      │
│      │  • Anchored to start of input                             │
│      │  • First match only                                       │
│      ▼                                                           │
│  ╔═══════════════════════════════════════════════════════════╗   │
│  ║  PIKA PARSER  ◄── OmniLisp uses this                      ║   │
│  ║      • Left recursion support (expr <- expr '+' term)     ║   │
│  ║      • Substring matching (find anywhere in input)        ║   │
│  ║      • All non-overlapping matches                        ║   │
│  ║      • Grammar composition (named rules)                  ║   │
│  ║      • Bottom-up DP with memoization                      ║   │
│  ╚═══════════════════════════════════════════════════════════╝   │
│      │                                                           │
│      ▼                                                           │
│  PCRE (practical regex)                                          │
│      • Has backreferences (\1, \2) - Pika lacks this             │
│      • But backreferences cause exponential blowup               │
│                                                                  │
└─────────────────────────────────────────────────────────────────┘
```

### Pika-Specific Features (All Preserved)

| Feature | Standard PEG | Pika | Status |
|---------|--------------|------|--------|
| Left recursion | ❌ Fails | ✅ Native | ✅ Preserved |
| Substring match | ❌ Anchored | ✅ Anywhere | ✅ Preserved |
| Multiple matches | ❌ First only | ✅ All | ✅ Preserved |
| Indirect recursion | ❌ Fails | ✅ Works | ✅ Preserved |
| Grammar composition | Limited | ✅ Full | ✅ Preserved |
| Regex compatibility | N/A | ❌ None | ✅ NEW |

### Current State (Post-Implementation)

`omni_compile_pattern()` at `src/runtime/pika/omni_grammar.c:1788-1829` now supports:
- ✅ All character classes: `[abc]`, `[a-z]`, `[^0-9]`
- ✅ All quantifiers: `*`, `+`, `?`
- ✅ Alternation: `a|b`
- ✅ Grouping: `(ab)+`
- ✅ Escape sequences: `\d`, `\w`, `\s`, `\.`, `\n`, `\t`, `\\`
- ✅ Dot: `.`
- ✅ Anchors: `^`, `$` (simplified)
- ✅ Nested groups: `((a|b)c)+`

Through a **token → AST → PikaClause** pipeline that bypasses the broken `pika_meta_parse()`.

### Phase 1-7: ✅ COMPLETED

All tasks from Phase 1 (Analysis) through Phase 7 (Documentation) are complete.
See git history for implementation details.

### Phase 8: Pika-Specific Advanced API (SUPERSEDED)

**Status**: SUPERSEDED by Phase 10 (Grammar DSL)

These C-level APIs are no longer needed. The grammar DSL provides the same functionality at the OmniLisp level:

- [N/A] **T-pika-adv-struct**: `OmniCompiledGrammar` → Grammar DSL values
- [N/A] **T-pika-adv-grammar-match**: `omni_pika_grammar_match()` → `(pika-match grammar :rule input)`
- [N/A] **T-pika-adv-find-positions**: `omni_pika_find_all_positions()` → DSL with position tracking
- [N/A] **T-pika-adv-grammar-find-all**: `omni_pika_grammar_find_all()` → `(pika-find-all grammar :rule input)`
- [N/A] **T-pika-adv-compiled**: Grammar caching → DSL grammars are naturally cached as values
- [N/A] **T-pika-adv-result-free**: Result freeing → handled by ASAP memory management

### Phase 9: Advanced API Tests (SUPERSEDED)

**Status**: SUPERSEDED - Tests will be part of Phase 10 (T-grammar-dsl-tests)

- [N/A] **T-pika-adv-test-left-rec**: Covered by `T-grammar-dsl-tests`
- [N/A] **T-pika-adv-test-positions**: Covered by `T-grammar-dsl-tests`
- [N/A] **T-pika-adv-test-compiled**: Covered by `T-grammar-dsl-tests`

---

## Phase 12: Pattern Compiler Fixes

Address issues and concerns identified in the current `omni_compile_pattern()` implementation.

### Tasks

- [R] **T-pattern-dead-code**: Remove dead code from omni_grammar.c
  - **Objective**: Clean up unused functions to reduce maintenance burden
  - **Where**: `src/runtime/pika/omni_grammar.c`
  - **What to change**:
    - Remove `is_meta_char()` (line ~1066)
    - Remove `is_quantifier()` (line ~1075)
    - Remove `is_quantifier_tok()` (line ~1344)
    - Remove `sb_free()` (line ~1526)
    - Remove `regex_to_peg()` (line ~1668) - entire PEG text generator (~150 lines)
  - **How to verify**: `make clean && make` compiles without warnings about unused functions
  - **Acceptance**:
    - No unused function warnings
    - All tests still pass

- [R] **T-pattern-dot-fix**: Fix DOT (any character) implementation
  - **Objective**: Make `.` match any character, not just hardcoded set
  - **Where**: `src/runtime/pika/omni_grammar.c` in `ast_to_clause()` RAST_DOT case
  - **What to change**:
    - Option A: Use `pika_clause_any()` if available
    - Option B: Use inverted empty charset `pika_clause_charset_invert()`
    - Option C: Use very large charset including all printable + common non-printable
  - **How to verify**: Test matching `.` against unicode characters, null bytes
  - **Acceptance**:
    - `.` matches characters outside the current hardcoded set
    - Document any remaining limitations

- [R] **T-pattern-anchors-fix**: Implement proper anchor semantics
  - **Objective**: Make `^` and `$` work correctly
  - **Where**: `src/runtime/pika/omni_grammar.c` in `ast_to_clause()` and `omni_pika_match()`
  - **What to change**:
    - `^` (start): Check match position at API level - filter matches to only accept those at position 0
    - `$` (end): Use `pika_clause_not_followed_by(pika_clause_charset_invert(pika_clause_char('\0')))`
    - Added tests: `pika_pattern_end_anchor`, `pika_pattern_start_anchor`
  - **How to verify**:
    - `^foo` only matches "foo" at start of input
    - `foo$` only matches "foo" at end of input
  - **Acceptance**:
    - Anchor tests pass with correct semantics (81/81 tests passing)
    - Behavior documented

- [R] **T-pattern-null-checks**: Add NULL checks for allocations
  - **Objective**: Prevent crashes on allocation failure
  - **Where**: `src/runtime/pika/omni_grammar.c`
  - **What to change**:
    - Add NULL check after `token_array_init()` (line ~1093)
    - Add NULL check after AST node allocations (lines ~1227-1232)
    - Add NULL check after unary node children array (lines ~1273-1276)
    - Return gracefully with error message on failure
  - **How to verify**: Code review; optionally test with malloc failure injection
  - **Acceptance**:
    - All malloc/calloc calls have NULL checks
    - Graceful error handling, no crashes

- [R] **T-pattern-negated-test**: Add negated character class test
  - **Objective**: Verify `[^...]` works correctly
  - **Where**: `tests/test_pika_tower.c`
  - **What to change**:
    - Add test:
      ```c
      TEST(pika_pattern_negated_charset) {
          Value* result = omni_pika_match("[^0-9]+", "abc123");
          ASSERT(result != NULL);
          ASSERT_STR_EQ(result->s, "abc");
      }
      ```
  - **How to verify**: `make && ./test_pika_tower`
  - **Acceptance**:
    - Test passes
    - Negated charsets match correctly

- [R] **T-pattern-stringbuf-leak**: Fix potential memory leak in StringBuffer
  - **Objective**: Prevent memory leak if realloc fails
  - **Where**: `src/runtime/pika/omni_grammar.c` in `sb_append()`
  - **What to change**:
    - Resolved by removing entire PEG text generation code (StringBuffer, regex_to_peg, etc.)
    - Dead code removed in T-pattern-dead-code task
  - **How to verify**: Code review
  - **Acceptance**:
    - All StringBuffer-related functions removed
    - No memory leak concerns for removed code

- [R] **T-pattern-charset-edge**: Handle character class edge cases
  - **Objective**: Support edge cases in character class syntax and escape sequences
  - **Where**: `src/runtime/pika/omni_grammar.c` in `regex_tokenize()`
  - **What to change**:
    - Handle `[]]` (literal `]` as first char)
    - Handle `[-az]` or `[az-]` (literal `-` at start/end)
    - Handle escaped `]` when scanning for closing bracket
    - Process escape sequences in charsets: `\n`, `\t`, `\r`, `\0`, `\\`, `\[`, `\]`, `\-`
    - Two-pass algorithm: calculate processed length, then copy with resolved escapes
  - **How to verify**: Added `pika_pattern_charset_escapes` test with comprehensive coverage
  - **Acceptance**:
    - Edge case tests pass
    - Escape sequences properly resolved
    - Known limitation documented: `\^` in charsets causes Pika library crash

---

## Known Issues

### Escaped Caret in Character Classes

**Status:** Pika Library Limitation

Using `\^` (escaped caret) inside a character class causes a crash in the Pika library. For example:
- `[\^]+` - crashes with SIGSEGV in `fnv1a_hash()`

**Root Cause:** The Pika library appears to have special handling for `^` as a negation operator that conflicts with using it as a literal character. When the tokenizer processes `[\^]` and extracts the inner content as `^`, the Pika clause construction fails.

**Workaround:** Don't use `\^` in character classes. If you need to match literal `^` characters, use alternative patterns:
- For negation: Use `[^...]` at the start of the charset (e.g., `[^abc]`)
- For literal `^`: Match it outside of character classes or use a different approach

**Other Escape Sequences Work:** All other escape sequences in character classes work correctly:
- `[\]]` - matches `]`
- `[\[]` - matches `[`
- `[\\-c]` - after processing, becomes range `[a-c]`
- `[\n]` - matches newline
- `[\t]` - matches tab

### DOT Implementation Limitations

The `.` (any character) uses `pika_clause_charset_invert(pika_clause_char('\0'))` which matches any character except NULL.

**Limitations:**
- Does NOT match NULL bytes (`\0`)
- Works correctly for all C strings (which don't contain NULL bytes)
- For binary data with embedded NULLs, would need different implementation

**Practical Impact:** For text processing, this limitation is irrelevant since C strings are NULL-terminated.

---

## Files to Modify

| File | Changes |
|------|---------|
| `src/runtime/pika/omni_grammar.h` | [DONE] Added Pika power hierarchy docs, advanced API types |
| `src/runtime/pika/omni_grammar.c` | [DONE] Lexer, parser, codegen; [TODO] cleanup dead code |
| `src/runtime/pika/pika_reader.c` | [R] Grammar DSL reader support |
| `src/runtime/pika_c/pika.c` | [R] Fix pika_meta_parse |
| `src/runtime/tower/tower.c` | [R] Add pika-match, pika-find-all primitives |
| `tests/test_pika_tower.c` | [DONE] Pattern tests; [R] Grammar DSL tests |
| `docs/PATTERN_SYNTAX.md` | [DONE] Full parsing hierarchy documentation |
| `docs/GRAMMAR_DSL.md` | [R] New grammar DSL documentation |

---

## References

- "Collapsing Towers of Interpreters" (Amin & Rompf, POPL 2018)
- "Pika parsing: reformulating packrat parsing as a dynamic programming algorithm solves the left recursion and error recovery problems" (Luke Hutchison, 2020)
- "Parsing Expression Grammars: A Recognition-Based Syntactic Foundation" (Bryan Ford, 2004)
