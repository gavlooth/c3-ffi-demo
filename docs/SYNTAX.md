# OmniLisp Syntax Reference

This document describes the complete syntax of OmniLisp, from lexical tokens to high-level expressions.

## Table of Contents

1. [Lexical Syntax](#lexical-syntax)
2. [Primitive Types](#primitive-types)
3. [Expressions](#expressions)
4. [Definitions](#definitions)
5. [Pattern Matching](#pattern-matching)
6. [Type Annotations](#type-annotations)

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
<decimal> ::= [0-9]+
<signed>  ::= (+|-)? <decimal>
```

Examples: `42`, `0`, `-123`, `+456`

#### Floats
```
<float> ::= <decimal> "." <decimal>?
        |  <decimal>? "." <decimal>
```

Examples: `3.14`, `0.5`, `.5`, `3.`

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
- `&` - Excluded (reserved for future use)
- `:` - Used for type annotations (`{Type}`)
- `;` - Used for comments

**Convention (not enforced):**
`!` and `?` are typically only at the **start or end** of symbols:
- At end: `set!`, `define!`, `null?`, `empty?`
- At start: `!not`, `!null`, `?maybe`, `?value`
- Not in middle: `foo!bar`, `set!value` (conventionally weird)

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
50%off             ; % in middle
3/4                ; / in middle

; Invalid (can't start with digits)
123foo             ; integer, not symbol
3d                 ; digit first
7up                ; digit first

; Invalid (reserved for syntax)
.foo               ; . for paths
foo.bar            ; . for paths (not a single symbol)
@meta              ; @ for metadata
#reader           ; # for reader macros
&and               ; & excluded
:type              ; : for types
comment;more       ; ; for comments
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

Examples: `"hello"`, `"world\n"`, `"escaped \"quote\""`

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

### Keywords

Keywords are symbols prefixed with `:` and evaluate to themselves.

```
<keyword> ::= ':' <symbol>
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
| **Nothing** | Singleton nil value | `nothing` |

---

## Expressions

### Literals

```lisp
42          ; integer
3.14        ; float
"hello"     ; string
#\a         ; character
'foo        ; symbol
true        ; boolean
false       ; boolean
nothing     ; nothing
```

### Lists

```lisp
()                    ; empty list
'(1 2 3)             ; list of integers
(list 1 2 3)         ; same, using function
'(a b c)             ; list of symbols
```

### Arrays

```lisp
[]                   ; empty array
[1 2 3]              ; array of integers
["a" "b" "c"]        ; array of strings
```

### Dictionaries (Property Lists)

```lisp
#{}                  ; empty dictionary
#{:a 1 :b 2}         ; dictionary with keys :a, :b
```

---

## Definitions

### Function Definitions

OmniLisp supports multiple syntaxes for function definitions.

#### Scheme-style (traditional)

```lisp
(define (add x y)
  (+ x y))

(define (square x)
  (* x x))
```

#### Slot syntax (explicit parameters)

```lisp
(define add [x y]
  (+ x y))

(define (add [x] [y])
  (+ x y))
```

#### Typed parameters

```lisp
(define add [x {Int}] [y {Int}] {Int}
  (+ x y))

(define process [x {String}] {Int}
  (string-length x))
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

### Basic Patterns

```lisp
(match value
  1        "one"
  2        "two"
  _        "other")
```

### Destructuring Patterns

```lisp
(match [1 2 3]
  [x y z]     (+ x y z)
  _           0)

(match (list 1 (list 2 3))
  [x [y z]]   (+ x y z)
  _           0)
```

### Type Patterns

```lisp
(match value
  [n {Int}]       "is integer"
  [s {String}]    "is string"
  _               "other")
```

### Guards

```lisp
(match value
  [x (when (> x 0))]  "positive"
  [x (when (< x 0))]  "negative"
  _                   "zero")
```

---

## Type Annotations

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

---

## Special Forms

### Conditionals

```lisp
(if condition
  then-expr
  else-expr)
```

### Let Bindings

```lisp
(let [x 10] [y 20]
  (+ x y))

(let [[x y] my-vec]
  (+ x y))

(let ^:seq [x 1] [y (+ x 1)]
  (* x y))
```

### Lambdas

```lisp
(lambda [x] (* x x))
(fn [x] (* x x))
(λ [x] (* x x))
```

---

## Path Expressions

Paths use `.` to access fields and properties.

```lisp
object.field
module.submodule.function
person.address.city
```

---

## Metadata

Metadata uses the `^` prefix.

```lisp
^:where [T {Number}]
^:parent {Number}
```

---

## Reader Macros

Special syntax starting with `#`:

```lisp
#val 42          ; value-to-type conversion
#{:a 1 :b 2}     ; dictionary
#\newline         ; character literal
#fmt"Hello $x"   ; format string (experimental)
```

---

## Comments

Comments start with `;` and continue to end of line.

```lisp
; This is a comment
(define x 42)  ; inline comment
```

---

## Operator Precedence

Operators (from highest to lowest precedence):

1. `.` - Path access
2. Function application
3. Unary `+`, `-`, `!`
4. `*`, `/`, `%`
5. `+`, `-`
6. `<`, `>`, `<=`, `>=`
7. `=`, `!=`
8. `&&` (logical and)
9. `||` (logical or)

---

## Summary

OmniLisp syntax combines:
- **Lisp-like** s-expressions for code structure
- **Julia-like** type system with first-class types
- **Modern** features like pattern matching and metadata
- **Clear** lexical rules to avoid ambiguity

For more details, see:
- `QUICK_REFERENCE.md` - Complete language reference
- `TYPE_SYSTEM_DESIGN.md` - Type system details
- `PATTERN_SYNTAX.md` - Pattern matching syntax
