# OmniLisp Pattern Matching and Parsing

OmniLisp uses **Pika Parser** for all string pattern matching and parsing operations. Pika is more powerful than both traditional regular expressions and standard PEG parsers.

## Parsing Power Hierarchy

```
┌─────────────────────────────────────────────────────────────────────────┐
│                       PARSING POWER HIERARCHY                            │
├─────────────────────────────────────────────────────────────────────────┤
│                                                                          │
│  Level 1: Formal Regular Expressions (DFA/NFA)                           │
│  ─────────────────────────────────────────────                           │
│      • Recognizes: Regular languages only                                │
│      • Limitations: No nesting, no recursion, no counting                │
│      • Cannot match: Balanced parentheses, nested structures             │
│      • Complexity: O(n) guaranteed                                       │
│      • Examples: grep, awk (basic mode)                                  │
│                                                                          │
│                              │                                           │
│                              ▼                                           │
│                                                                          │
│  Level 2: Standard PEG (Packrat Parsing)                                 │
│  ───────────────────────────────────────                                 │
│      • Recognizes: Context-free grammars (and some beyond)               │
│      • Limitations:                                                      │
│          - NO left recursion (causes infinite loop)                      │
│          - Anchored to start of input only                               │
│          - Returns first match only                                      │
│      • Complexity: O(n) with memoization                                 │
│      • Examples: Most PEG libraries, parser combinators                  │
│                                                                          │
│                              │                                           │
│                              ▼                                           │
│                                                                          │
│  ╔═══════════════════════════════════════════════════════════════════╗   │
│  ║  Level 3: PIKA PARSER  ◄── OmniLisp uses this                     ║   │
│  ║  ════════════════════════════════════════════                     ║   │
│  ║                                                                   ║   │
│  ║  UNIQUE CAPABILITIES:                                             ║   │
│  ║                                                                   ║   │
│  ║  1. LEFT RECURSION SUPPORT                                        ║   │
│  ║     Standard PEG fails on: expr <- expr '+' term                  ║   │
│  ║     Pika handles it natively - essential for expression parsing   ║   │
│  ║                                                                   ║   │
│  ║  2. SUBSTRING MATCHING                                            ║   │
│  ║     Standard PEG: Must match from position 0                      ║   │
│  ║     Pika: Finds matches ANYWHERE in input                         ║   │
│  ║     Example: match("[0-9]+", "abc123def") → "123"                 ║   │
│  ║                                                                   ║   │
│  ║  3. ALL NON-OVERLAPPING MATCHES                                   ║   │
│  ║     Standard PEG: Returns first match only                        ║   │
│  ║     Pika: Returns ALL matches via find_all                        ║   │
│  ║     Example: find_all("[0-9]+", "a1b22c333") → ("1" "22" "333")   ║   │
│  ║                                                                   ║   │
│  ║  4. INDIRECT/MUTUAL RECURSION                                     ║   │
│  ║     Rules can reference each other cyclically                     ║   │
│  ║     Example: A <- B 'x' / 'a'                                     ║   │
│  ║              B <- A 'y' / 'b'                                     ║   │
│  ║                                                                   ║   │
│  ║  5. GRAMMAR COMPOSITION                                           ║   │
│  ║     Build complex parsers from named, reusable rules              ║   │
│  ║                                                                   ║   │
│  ║  Algorithm: Bottom-up dynamic programming with memoization        ║   │
│  ║  Complexity: O(n³) worst case, typically O(n²) or better          ║   │
│  ║                                                                   ║   │
│  ╚═══════════════════════════════════════════════════════════════════╝   │
│                              │                                           │
│                              ▼                                           │
│                                                                          │
│  Level 4: PCRE (Practical Regular Expressions)                           │
│  ─────────────────────────────────────────────                           │
│      • Has backreferences: (\w+)\s+\1 matches "the the"                  │
│      • Pika CANNOT do backreferences                                     │
│      • BUT: Backreferences cause exponential worst-case complexity       │
│      • Trade-off: Pika is more predictable, PCRE more expressive         │
│                                                                          │
└─────────────────────────────────────────────────────────────────────────┘
```

## Feature Comparison Table

| Feature | Formal Regex | Standard PEG | Pika | PCRE |
|---------|--------------|--------------|------|------|
| Simple patterns `a+`, `[0-9]*` | Yes | Yes | Yes | Yes |
| Alternation `a\|b` | Yes | Yes (`/`) | Yes | Yes |
| Grouping `(ab)+` | Yes | Yes | Yes | Yes |
| Lookahead `(?=...)` | No | Yes (`&`) | Yes | Yes |
| Negative lookahead `(?!...)` | No | Yes (`!`) | Yes | Yes |
| Balanced parens `((()))` | **No** | Yes | Yes | Yes* |
| Left recursion | **No** | **No** | **Yes** | **No** |
| Substring matching | Varies | **No** | **Yes** | Yes |
| All matches | Varies | **No** | **Yes** | Yes |
| Backreferences `\1` | **No** | **No** | **No** | **Yes** |
| Grammar rules | **No** | Yes | **Yes** | **No** |
| Predictable complexity | Yes O(n) | Yes O(n) | Yes O(n³) | **No** |

## OmniLisp Pattern API

### Tier 1: Simple Patterns (Regex-like)

For common string operations, use the simple pattern syntax:

```lisp
;; Match first occurrence
(pika-match "[a-z]+" "Hello World")     ; → "ello"

;; Find all matches
(pika-find-all "[0-9]+" "a1b22c333")    ; → ("1" "22" "333")

;; Split by pattern
(pika-split "," "a,b,c,d")              ; → ("a" "b" "c" "d")

;; Replace pattern
(pika-replace "[0-9]+" "X" "a1b2c3" #t) ; → "aXbXcX"
```

#### Supported Pattern Syntax

The `omni_compile_pattern()` function now supports an extended regex-like syntax through a token → AST → PikaClause pipeline:

| Pattern | Meaning | Example |
|---------|---------|---------|
| `[abc]` | Character class | `[aeiou]` matches vowels |
| `[a-z]` | Character range | `[A-Za-z]` matches letters |
| `[^abc]` | Negated class | `[^0-9]` matches non-digits |
| `.` | Any character | `a.c` matches "abc", "a1c" |
| `*` | Zero or more | `a*` matches "", "a", "aaa" |
| `+` | One or more | `a+` matches "a", "aaa" |
| `?` | Zero or one | `colou?r` matches "color", "colour" |
| `\|` | Alternation | `cat\|dog` matches "cat" or "dog" |
| `(...)` | Grouping | `(ab)+` matches "ab", "abab" |
| `\d` | Digit shorthand | `\d+` matches "123" (equivalent to `[0-9]+`) |
| `\w` | Word char shorthand | `\w+` matches "hello_123" (equivalent to `[a-zA-Z0-9_]+`) |
| `\s` | Whitespace shorthand | `\s+` matches spaces/tabs/newlines (equivalent to `[ \t\n\r]+`) |
| `\n`, `\t`, `\r` | Escape sequences | `\n` matches newline |
| `\\` | Literal backslash | `\\` matches a single backslash |
| `\.` | Literal dot | `3\.14` matches "3.14" |
| `^` | Start anchor | `^foo` matches "foo" at start (currently simplified) |
| `$` | End anchor | `foo$` matches "foo" at end (currently simplified) |

**Implementation Notes:**
- Patterns are tokenized into a stream of tokens (literals, charsets, operators, escapes)
- Tokens are parsed into an AST following regex precedence (alternation < sequence < quantifier < atom)
- AST nodes are converted directly to PikaClause structures (no PEG text intermediate)
- Anchors (`^`, `$`) are currently simplified - start anchors are implicit in PEG matching

### Tier 2: Full Pika Grammars (Advanced)

For complex parsing that requires left recursion or grammar composition:

```lisp
;; Define a grammar for arithmetic expressions
(define arith-grammar "
  expr   <- expr '+' term / expr '-' term / term
  term   <- term '*' factor / term '/' factor / factor
  factor <- '(' expr ')' / number
  number <- [0-9]+
")

;; Parse with the grammar
(pika-grammar-match arith-grammar "expr" "1+2*3")
; → Parses as: 1 + (2 * 3) due to left recursion handling

;; This would FAIL in standard PEG parsers!
;; But Pika handles left recursion natively.
```

#### Grammar Syntax (PEG)

```
rule_name <- expression

;; Expressions:
'literal'        ;; Literal string
"literal"        ;; Literal string (alternative)
[a-z]            ;; Character class
.                ;; Any character
rule_name        ;; Reference to another rule
e1 e2            ;; Sequence (match e1 then e2)
e1 / e2          ;; Ordered choice (try e1, else e2)
e*               ;; Zero or more
e+               ;; One or more
e?               ;; Optional
&e               ;; Positive lookahead (match without consuming)
!e               ;; Negative lookahead (fail if matches)
(e)              ;; Grouping
```

## Why Pika Over Regex?

### 1. Parse Nested Structures

```lisp
;; Regex CANNOT do this - Pika can:
(define nested-parens "
  balanced <- '(' balanced* ')' / [^()]+
")
(pika-grammar-match nested-parens "balanced" "((a)(b(c)))")  ; Works!
```

### 2. Parse Programming Languages

```lisp
;; Left-recursive expression grammar - impossible in standard PEG
(define expr-grammar "
  expr   <- expr ('+' / '-') term / term
  term   <- term ('*' / '/') factor / factor
  factor <- '(' expr ')' / [0-9]+
")
;; Pika handles this; packrat parsers would infinite loop
```

### 3. Find All Matches Anywhere

```lisp
;; Standard PEG only matches from start
;; Pika finds matches anywhere in the string
(pika-find-all "[A-Z][a-z]+" "Hello World Test")
; → ("Hello" "World" "Test")
```

### 4. Predictable Performance

Unlike PCRE with backreferences (which can have exponential blowup), Pika has polynomial worst-case complexity. No regex denial-of-service attacks.

## What Pika Cannot Do

**Backreferences** - matching the same text twice:

```lisp
;; PCRE can do this, Pika cannot:
;; (\w+)\s+\1 matches "the the" but not "the a"

;; Workaround: Use a custom matcher or post-process results
```

This is the only significant feature PCRE has that Pika lacks. For most use cases, Pika's other advantages (left recursion, grammar composition, predictable performance) outweigh this limitation.

## Implementation Details

The pattern matching system is implemented in:
- `src/runtime/pika/omni_grammar.c` - Grammar definition, pattern compiler, regex-to-AST converter
- `src/runtime/pika/omni_grammar.h` - Public API
- `src/runtime/pika_c/pika.c` - Core Pika parser implementation

### Architecture

The upgraded `omni_compile_pattern()` function follows a three-stage pipeline:

```
Regex Pattern → Tokens → AST → PikaClauses → Grammar
                [Lexer]   [Parser]   [Codegen]  [Builder]
```

1. **Lexer (`regex_tokenize`)**: Scans the regex pattern into tokens
   - Literal characters, character classes, operators, escapes
   - Reports syntax errors with position information

2. **Parser (`parse_pattern`)**: Builds an AST following regex precedence
   - Atom: literals, charsets, dot, escapes, parenthesized expressions
   - Quantifier: `*`, `+`, `?` bind to preceding atom
   - Sequence: implicit concatenation (e.g., `abc` is `(ab)c`)
   - Alternation: `|` has lowest precedence

3. **Codegen (`ast_to_clause`)**: Converts AST to PikaClause structures
   - Direct clause construction (no PEG text intermediate)
   - Bypasses the broken `pika_meta_parse()` function

Key functions:
- `omni_pika_match()` - Match pattern against string
- `omni_pika_find_all()` - Find all matches
- `omni_pika_split()` - Split by pattern
- `omni_pika_replace()` - Search and replace
- `omni_pika_match_rule()` - Match using a specific rule from a grammar
- `omni_compile_pattern()` - Compile regex-like pattern (NEW: uses token → AST → clause pipeline)
- `omni_compile_peg()` - Compile full PEG grammar (currently relies on broken `pika_meta_parse`)

## References

- Luke Hutchison, "Pika parsing: reformulating packrat parsing as a dynamic programming algorithm solves the left recursion and error recovery problems" (2020)
- Bryan Ford, "Parsing Expression Grammars: A Recognition-Based Syntactic Foundation" (2004)
- Aho, Sethi, Ullman, "Compilers: Principles, Techniques, and Tools" - for formal language hierarchy
