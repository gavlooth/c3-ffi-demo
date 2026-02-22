# OmniLisp Syntax Reference

This document describes the complete syntax of OmniLisp, from lexical tokens to high-level expressions.

> **AUTHORITATIVE DIRECTIVE:** This document describes **OmniLisp syntax only**.
> Do NOT assume features from other Lisp dialects (Scheme, Common Lisp, Clojure, Racket, etc.).
> If a feature is not documented here, it does not exist in OmniLisp.
> When in doubt, check the Implementation Status section.

> **Implementation Status Note (2026-01-17):** This document describes the *design* syntax.
> See the [Implementation Status](#implementation-status) section at the end for what is
> currently implemented in the Omni parser (`csrc/parser/parser.c`).

## Table of Contents

1. [Character Calculus (Core Principle)](#character-calculus-core-principle)
2. [Lexical Syntax](#lexical-syntax)
3. [Primitive Types](#primitive-types)
4. [Collection Types](#collection-types)
5. [Expressions](#expressions)
6. [Definitions](#definitions)
7. [Pattern Matching](#pattern-matching)
8. [Type Annotations](#type-annotations)
9. [Algebraic Effects](#algebraic-effects)
10. [Special Forms](#special-forms)
11. [Metadata](#metadata)
12. [Reader Macros](#reader-macros)
13. [Implementation Status](#implementation-status)

---

## Character Calculus (Core Principle)

OmniLisp adheres to a strict **Character Calculus** that gives each bracket type a specific domain:

| Character | Domain | Purpose |
| :--- | :--- | :--- |
| **`{}`** | **Kind** | Type annotations, blueprints, and the domain of static blueprints. |
| **`[]`** | **Slot** | Arguments, parameters, field lists, and data-carrying sequences (Data Domain). |
| **`()`** | **Flow** | Execution, value construction, and type-generating calculations (Execution Domain). |
| **`^`**  | **Metadata** | Out-of-band instructions, relationships, and constraints. |

This principle guides all syntax decisions:
- Types go in braces: `{Int}`, `{String}`, `{List T}`
- Data goes in brackets: `[1 2 3]`, `[x {Int}]`
- Execution goes in parens: `(+ 1 2)`, `(define ...)`
- Metadata uses caret: `^:mutable`, `^:where`

---

## Lexical Syntax

### Whitespace

- **Spaces** (`U+0020`): Significant for separating tokens
- **Tabs** (`U+0009`): Treated as spaces
- **Newlines** (`U+000A`, `U+000D`): Treated as whitespace
- **Comments**: Start with `;` and continue to end of line

### Numbers

#### Integers
```
<integer> ::= [+-]? [0-9]+
```

Examples: `42`, `0`, `-123`, `+456`

#### Floats
```
<float> ::= [+-]? ( [0-9]+ "." [0-9]* | [0-9]* "." [0-9]+ )
```

Examples: `3.14`, `-0.5`, `.5`, `3.`, `+2.718`

### Symbol Character Rules

Symbols in OmniLisp follow specific character rules to maintain clarity and avoid ambiguity with other syntax.

**Start with (first character):**
- Letters: `a-z`, `A-Z`
- Operators: `*`, `!`, `-`, `_`, `?`, `%`, `/`, `=`, `<`, `>`

**Excluded from start:**
- Digits: `0-9` (to avoid confusion with integers)
- Reserved syntax: `.`, `@`, `#`, `&`, `:`, `;`

**Middle/subsequent characters:**
- All of the above (letters + operators)
- **Plus:** Digits `0-9`

**Excluded entirely:**
- `.` - Used for module paths (`Math.sin`)
- `@` - Used for metadata (`^:where`)
- `#` - Used for reader macros (`#val`, `#\newline`)
- `&` - Reserved for rest patterns (`& rest`)
- `:` - Used for type annotations (`{Type}`) and colon-quoted symbols
- `;` - Used for comments

**Examples:**

```lisp
; Valid symbols
foo                ; letters
foo-bar            ; - as separator
foo123             ; digits in middle
x1_y2              ; _ as separator
set!               ; ! at end
null?              ; ? at end
!not               ; ! at start
?maybe             ; ? at start
*                  ; single operator
<=                 ; comparison operators
==                 ; equality

; Invalid (can't start with digits)
123foo             ; integer, not symbol
3d                 ; digit first

; Invalid (reserved for syntax)
.foo               ; . for paths
foo.bar            ; . for paths (not a single symbol)
@meta              ; @ for metadata
#reader            ; # for reader macros
:type              ; : for colon-quoted symbols
```

### Strings

Strings are delimited by double quotes (`"`) and support escape sequences.

```
<string> ::= '"' <string-char>* '"'
```

Escape sequences:
- `\"` - Double quote
- `\\` - Backslash
- `\n` - Newline
- `\t` - Tab
- `\r` - Carriage return
- `\xNN` - Hex character code

Examples: `"hello"`, `"world\n"`, `"escaped \"quote\""`, `"\x41"` (= "A")

### Characters

Character literals use the `#\` prefix.

```
<char> ::= '#\' <named-char>
        |  '#\' 'x' <hex> <hex>
```

Named characters:
- `#\newline` - Newline (U+000A)
- `#\space` - Space (U+0020)
- `#\tab` - Tab (U+0009)

Hex characters:
- `#\x41` - Character with hex code 41 ('A')
- `#\x00` - Null character

### Colon-Quoted Symbols (`:name`)

OmniLisp does **not** have a separate "keyword" type.
Instead, `:name` is **pure reader sugar** for a quoted symbol:

```
:name  ≡  'name  ≡  (quote name)
```

Grammar:
```
<colon-quoted-symbol> ::= ':' <symbol>
```

Examples: `:foo`, `:my-keyword`

---

## Primitive Types

| Type | Description | Examples |
|------|-------------|----------|
| **Integer** | Arbitrary precision integers | `42`, `-123`, `0` |
| **Float** | Double-precision floating point | `3.14`, `-0.5`, `1.0` |
| **String** | Sequence of characters | `"hello"` |
| **Character** | Single Unicode codepoint | `#\a`, `#\newline` |
| **Symbol** | Identifier/symbol | `'foo`, `bar` |
| **Boolean** | True or false | `true`, `false` |
| **Nothing** | Singleton "no value" value | `nothing` |

---

## Collection Types

OmniLisp uses exactly **3 core collection types**:

| Type | Syntax | Character | Use Case |
|------|--------|-----------|----------|
| **List** | `(1 2 3)` or `'(1 2 3)` | `()` Flow | Cons cells, code representation, recursive processing |
| **Array** | `[1 2 3]` | `[]` Slot | Mutable, indexed access, general sequences |
| **Dict** | `#{:a 1 :b 2}` | `#{}` | Key-value storage, structured data |

> **DESIGN DECISION (2026-01-15):** Tuples are deprecated. Use Arrays instead.
> Named tuples are deprecated. Use Dicts instead.
> This simplifies the mental model while providing equivalent functionality.

### Lists

Lists use `()` and represent cons cells (linked lists).

```lisp
()                    ; empty list
'()                   ; quoted empty list
'(1 2 3)              ; list of integers
(list 1 2 3)          ; same, using function
'(a b c)              ; list of symbols
```

Lists are the fundamental data structure for code representation (S-expressions).

### Arrays

Arrays use `[]` and provide mutable, indexed access.

```lisp
[]                   ; empty array
[1 2 3]              ; array of integers
["a" "b" "c"]        ; array of strings
[[1 2] [3 4]]        ; nested arrays
```

Arrays grow dynamically and support O(1) index access.

### Dictionaries

Dictionaries use `#{}` and provide key-value storage.

```lisp
#{}                  ; empty dictionary
#{:a 1 :b 2}         ; dictionary with symbol keys
#{:name "Alice" :age 30}
```

Keys are typically colon-quoted symbols (`:key`), which are equivalent to `'key`.

---

## Expressions

### Literals

```lisp
42          ; integer
-123        ; negative integer
3.14        ; float
"hello"     ; string
#\a         ; character
'foo        ; symbol
true        ; boolean true
false       ; boolean false
nothing     ; nothing value
```

### Function Application

```lisp
(+ 1 2)              ; addition
(print "hello")      ; function call
(map f xs)           ; higher-order function
```

### Path Expressions

Paths use `.` to access fields and module members.

```lisp
object.field
module.submodule.function
person.address.city
```

---

## Definitions

### Function Definitions

OmniLisp uses **Slot syntax** for function parameters: each parameter is in `[]` with optional type in `{}`.

#### Canonical Form (Recommended)

```lisp
;; With types
(define add [x {Int}] [y {Int}] {Int}
  (+ x y))

;; Without types
(define square [x]
  (* x x))

;; Mixed types
(define process [x] [y {Int}] [z {Float}]
  (body))
```

#### Shorthand Form (Desugars to Canonical)

```lisp
;; Shorthand - bare symbols become untyped slots
(define add x y
  (+ x y))

;; Equivalent to:
(define add [x] [y]
  (+ x y))
```

### Variable Definitions

```lisp
(define x 42)
(define name "Alice")
(define counter 0)
```

### Typed Variables

```lisp
(define x {Int} 42)
(define name {String} "Alice")
```

---

## Pattern Matching

The `match` form provides pattern matching with destructuring.

### Basic Patterns

```lisp
(match value
  [1 "one"]
  [2 "two"]
  [_ "other"])
```

### Variable Binding Patterns

```lisp
(match value
  [x (* x 2)])        ; binds x to value
```

### Destructuring Patterns

```lisp
(match [1 2 3]
  [[x y z] (+ x y z)])      ; binds x=1, y=2, z=3

(match [[1 2] 3]
  [[[a b] c] (+ a b c)])    ; nested destructuring
```

### As Patterns

```lisp
(match [1 2]
  [[x y] as pair] pair)     ; binds x=1, y=2, pair=[1 2]
```

### Guards

```lisp
(match value
  [x :when (> x 0) "positive"]
  [x :when (< x 0) "negative"]
  [_ "zero"])
```

### Rest Patterns

```lisp
(match [1 2 3 4 5]
  [[x y & rest] rest])      ; binds x=1, y=2, rest=[3 4 5]
```

---

## Type Annotations

Types are annotated using `{}` (Kind domain).

### Basic Annotations

```lisp
(define x {Int} 42)
(define name {String} "Alice")
```

### Function Type Annotations

```lisp
(define add [x {Int}] [y {Int}] {Int}
  (+ x y))

(define process [x {String}] {Int}
  (string-length x))
```

### Type Parameters

```lisp
(define {struct Pair [T]}
  [first {T}]
  [second {T}])
```

### Type Constraints (`^:where`)

```lisp
;; x and y must be the same type T, where T is a Number
(define add ^:where [T {Number}]
  [x {T}] [y {T}] {T}
  (+ x y))
```

---

## Algebraic Effects

OmniLisp uses **algebraic effects** for error handling and control flow.

> **IMPORTANT:** OmniLisp does NOT support `try`/`catch`. Use algebraic effects instead.

### Effect Declaration

```lisp
;; Declare an effect type
(define {effect Error}
  [raise [msg {String}] {bottom}])

;; Resumable effect
(define {effect Ask}
  [ask [prompt {String}] {String}])
```

### Performing Effects

Effects are performed like normal function calls:

```lisp
(define safe-div [x {Int}] [y {Int}] {Int}
  (if (= y 0)
    (raise "Division by zero")   ; perform effect
    (/ x y)))
```

### Handling Effects

```lisp
(handle
  (safe-div 10 0)
  [raise [msg] (println "Error: " msg) 0])    ; handle and return default

;; With resumption
(handle
  (ask "Name?")
  [ask [prompt] (resume "Alice")])            ; resume with value
```

### Effect Operation Semantics

**Key Insight:** Effect operations are **NOT** special forms. They are ordinary function calls
that are dynamically intercepted by the nearest enclosing `handle` form.

```lisp
;; `raise` is NOT a keyword - it's a normal function call
(raise "Division by zero")

;; This is equivalent to:
(perform 'raise "Division by zero")
```

**How it works:**
1. When an effect operation is called (e.g., `(raise msg)`), the runtime searches
   the call stack for a matching handler
2. The `handle` form installs a **prompt** (delimiter) with handler clauses
3. When an effect matches a handler clause, the continuation is captured up to that prompt
4. The handler clause receives the payload and optionally a **resumption** object

### Effect Row Type Signatures

Functions can declare which effects they may perform using the `^:effects` metadata:

```lisp
;; Function that may raise Error effect
(define safe-div [x {Int}] [y {Int}] {Int} ^:effects [{Error}]
  (if (= y 0)
    (raise "Division by zero")
    (/ x y)))

;; Function with multiple effects
(define interactive-compute [x {Int}] {Int} ^:effects [{Ask} {Emit}]
  (let [name (ask "Your name?")]
    (emit (string-append "Hello, " name))
    (* x 2)))

;; Pure function (no effects)
(define double [x {Int}] {Int}
  (* x 2))
```

**Effect rows are checked at compile time:**
- Calling an effectful function from a pure context is a type error
- Handlers must cover all effects that may be performed
- Effect inference tracks which effects flow through a computation

### Resume Semantics

The `resume` function is a **normal function** that is only valid within handler clauses.
It invokes the captured continuation (resumption) with a value.

**Type signature:**
```
resume : (Resumption a → a → b)
```

**Semantics:**
1. `resume` is passed as the second argument to handler clauses (after payload)
2. Calling `resume` continues the computation from where the effect was performed
3. The value passed to `resume` becomes the return value of the effect operation
4. Handlers can call `resume` zero times (abort), once (one-shot), or multiple times (multi-shot)

```lisp
;; Handler that does NOT resume (aborts computation)
(handle
  (begin (raise "error") (println "never reached"))
  [raise [msg] 0])  ; returns 0, computation aborted

;; Handler that resumes once (one-shot continuation)
(handle
  (+ 1 (ask "number?"))
  [ask [prompt resume] (resume 41)])  ; returns 42

;; Handler that resumes multiple times (multi-shot)
(handle
  (emit (choose [1 2 3]))
  [choose [options resume]
    (map resume options)])  ; calls resume for each option
```

**Implementation note:** Resumptions are implemented using delimited continuations.
The `handle` form installs a prompt, and `resume` invokes `cont_invoke()` on the
captured continuation.

### Why Not try/catch?

Algebraic effects provide:
1. **Type Safety**: Effects are part of the type signature
2. **Resumption**: Handlers can resume computation (impossible with exceptions)
3. **Composability**: Multiple effects compose without nested try blocks
4. **Explicit Control Flow**: No hidden control flow jumps
5. **Region Compatibility**: Works naturally with Region-RC memory management

---

## Special Forms

### Conditionals

```lisp
(if condition
  then-expr
  else-expr)
```

### Let Bindings

Let bindings use Slot `[]` syntax: `[name {Type}? value]`

```lisp
;; Untyped bindings
(let [x 10] [y 20]
  (+ x y))

;; Typed bindings
(let [x {Int} 10] [y {Int} 20]
  (+ x y))

;; Sequential let (each binding sees previous ones)
(let ^:seq [x 1] [y (+ x 1)]
  y)

;; Destructuring in let
(let [[a b] my-array]
  (+ a b))
```

### Lambdas

```lisp
(lambda [x] (* x x))
(fn [x] (* x x))
(λ [x] (* x x))

;; Multiple parameters
(fn [x] [y] (+ x y))

;; Typed parameters
(fn [x {Int}] [y {Int}] {Int}
  (+ x y))
```

### Quote

```lisp
'foo                  ; quoted symbol
'(1 2 3)              ; quoted list
`(a ,b c)             ; quasiquote with unquote
```

---

## Metadata

Metadata uses the `^` prefix and provides out-of-band instructions.

```lisp
^:parent {Number}        ; inheritance
^:where [T {Number}]     ; type constraints
^:mutable                ; mutability marker
^:covar                  ; covariance marker
^:seq                    ; sequential binding
```

Metadata can attach to definitions:

```lisp
(define ^:mutable {struct Player}
  [hp {Int32}]
  [name {String}])
```

---

## Reader Macros

Special syntax starting with `#`:

```lisp
#val 42              ; value-to-type conversion (singleton type)
#{:a 1 :b 2}         ; dictionary literal
#\newline            ; character literal
#fmt"Hello $x"       ; format string (experimental)
```

---

## Comments

Comments start with `;` and continue to end of line.

```lisp
; This is a comment
(define x 42)  ; inline comment
```

---

## Implementation Status

This section documents which syntax features are implemented in the Omni parser
(`csrc/parser/parser.c`) vs the analyzer (`csrc/analysis/analysis.c`).

### Fully Implemented (Parser)

| Feature | Parser Rule | Notes |
|---------|-------------|-------|
| Integers | `R_INT` | Positive and signed (`-123`, `+456`) |
| Floats | `R_FLOAT` | Full format including `.5` and `3.` |
| Symbols | `R_SYM` | Includes `-`, `+`, `*`, etc. as valid start chars |
| Strings | `R_STRING` | With escape sequences `\n`, `\t`, `\"`, `\\`, `\xNN` |
| Lists | `R_LIST` | S-expressions `(...)` |
| Arrays | `R_ARRAY` | Bracket syntax `[...]` |
| Dicts | `R_DICT` | Hash-brace syntax `#{...}` |
| Type literals | `R_TYPE` | Brace syntax `{Type}` |
| Paths | `R_PATH` | Dot notation `foo.bar.baz` |
| Quote | `R_QUOTED` | `'expr`, `` `expr ``, `,expr`, `,@expr` |
| Colon-quoted | `R_COLON_QUOTED_SYMBOL` | `:name` → `(quote name)` |
| Metadata keys | `R_META_KEY` | `^:parent`, `^:where`, etc. |
| Metadata attach | `R_METADATA` | `^ meta obj` (requires whitespace) |
| Named chars | `R_NAMED_CHAR` | `#\newline`, `#\space`, `#\tab`, `#\xNN` |
| Format strings | `R_FMT_STRING` | `#fmt"..."` |
| Value-to-type | `R_HASH_VAL` | `#val 42` → `(value->type 42)` |
| Comments | `R_COMMENT` | `;` to end of line |
| Match clauses | Codegen | `[pattern result]` and `[pattern :when guard result]` |

### Analyzer-Level Recognition

These are parsed as plain symbols but recognized specially in the analyzer:

| Symbol | Recognition | Location |
|--------|-------------|----------|
| `true` | Boolean true | `analysis.c`, `codegen.c` |
| `false` | Boolean false | `analysis.c`, `codegen.c` |
| `nothing` | Nothing value | `parser.c` (parser special-case) |
| `nil` | Nil value | `parser.c` (parser special-case) |
| `lambda` | Lambda form | `analysis.c` |
| `fn` | Lambda alias | `analysis.c` |
| `λ` (U+03BB) | Lambda alias | `analysis.c` |

### Partial Implementation

| Feature | Status | Notes |
|---------|--------|-------|
| Effect declarations | Runtime only | `{effect Name}` not yet parsed |
| `handle` form | Codegen done | Works via runtime primitives |
| Pattern bindings | Done | Variable extraction in match patterns |
| As patterns | Done | `[pattern as name]` syntax |
| Guards | Done | `:when` in match clauses |
| Rest patterns | Done | `[x y & rest]` syntax |

### NOT IMPLEMENTED

| Feature | Status |
|---------|--------|
| `try`/`catch` | **Intentionally NOT supported** - use algebraic effects |
| Tuples | **Deprecated** - use arrays |
| Named Tuples | **Deprecated** - use dicts |

---

## Quick Reference Summary

```lisp
;; Collections (ONLY these 3)
'(1 2 3)              ; List
[1 2 3]               ; Array
#{:a 1 :b 2}          ; Dict

;; Definitions
(define x 42)                          ; variable
(define add [x] [y] (+ x y))           ; function (slot syntax)
(define add [x {Int}] {Int} (* x x))   ; typed function

;; Pattern Matching
(match value
  [pattern result]
  [pattern :when guard result]
  [[x y] as pair (use pair)])

;; Algebraic Effects (NOT try/catch)
(handle
  (might-fail x)
  [raise [msg] default-value])

;; Types in {}
{Int}  {String}  {List T}  {Array Int}

;; Metadata with ^
^:mutable  ^:where [T {Number}]  ^:parent {Real}
```

---

## See Also

- `docs/QUICK_REFERENCE.md` - Complete language reference
- `docs/TYPE_SYSTEM_DESIGN.md` - Type system details
- `TODO.md` - Design Decisions section for rationale
