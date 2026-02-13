# Pika Lisp Language Specification

**Version:** 0.1.0
**Date:** 2026-02-11

Pika Lisp is a minimal Lisp dialect with first-class delimited continuations and algebraic effects. It runs on a region-based memory system implemented in C3.

---

## Table of Contents

1. [Syntax Overview](#1-syntax-overview)
2. [Data Types](#2-data-types)
3. [Special Forms](#3-special-forms)
4. [Path and Index Notation](#4-path-and-index-notation)
5. [Primitives](#5-primitives)
6. [Delimited Continuations](#6-delimited-continuations)
7. [Effect Handlers](#7-effect-handlers)
8. [REPL](#8-repl)
9. [Examples](#9-examples)

---

## 1. Syntax Overview

### 1.1 Lexical Elements

```
; Comments start with semicolon and extend to end of line

; Integers
42
-17
0

; Strings (double-quoted, with escape sequences)
"hello world"
"line1\nline2"
"tab\there"
"quote: \"nested\""

; Symbols (identifiers)
foo
my-function
string->list
null?

; Quote shorthand
'symbol     ; equivalent to (quote symbol)
'(a b c)    ; equivalent to (quote (a b c))
```

### 1.2 S-Expression Forms

```lisp
; Function application
(function arg1 arg2 ...)

; Curried application: (f a b c) => (((f a) b) c)
(+ 1 2)     ; adds 1 and 2

; Empty list / nil
()
```

---

## 2. Data Types

### 2.1 Primitive Types

| Type | Description | Example |
|------|-------------|---------|
| `nil` | Empty/false value | `nil`, `()` |
| `int` | 64-bit signed integer | `42`, `-17` |
| `string` | Immutable string (max 64 chars) | `"hello"` |
| `symbol` | Interned identifier | `'foo`, `lambda` |
| `cons` | Pair / list cell | `(cons 1 2)` |

### 2.2 Function Types

| Type | Description |
|------|-------------|
| `closure` | User-defined function with captured environment |
| `primitive` | Built-in function |
| `continuation` | Captured delimited continuation |

### 2.3 Truthiness

- **Falsy values:** `nil`, `false` symbol
- **Truthy values:** Everything else (including `0`)

---

## 3. Special Forms

### 3.1 `lambda` — Function Definition

```lisp
(lambda (param) body)
```

Creates a closure capturing the lexical environment.

```lisp
(lambda (x) (+ x 1))              ; increment function
(lambda (f) (lambda (x) (f x)))   ; higher-order function
```

**Note:** Currently single-parameter only. Multi-parameter functions use currying.

### 3.2 `define` — Global Definition

```lisp
(define name value)
```

Binds `name` to `value` in the global environment.

```lisp
(define pi 3)
(define inc (lambda (x) (+ x 1)))
```

### 3.3 `let` — Local Binding

```lisp
(let ((name value)) body)
```

Creates a local binding visible in `body`.

```lisp
(let ((x 10))
  (+ x 5))    ; => 15
```

### 3.4 `if` — Conditional

```lisp
(if test then-expr else-expr)
```

Evaluates `then-expr` if `test` is truthy, otherwise `else-expr`.

```lisp
(if (> x 0) "positive" "non-positive")
```

### 3.5 `quote` — Prevent Evaluation

```lisp
(quote datum)
'datum        ; shorthand
```

Returns `datum` unevaluated as a value.

```lisp
'foo          ; => symbol foo
'(1 2 3)      ; => list (1 2 3)
```

### 3.6 `match` — Pattern Matching

```lisp
(match expr
  (pattern1 result1)
  (pattern2 result2)
  ...)
```

Matches `expr` against patterns in order, evaluating the first matching result.

#### Patterns:
| Pattern | Description |
|---------|-------------|
| `_` | Wildcard — matches anything |
| `x` | Variable — matches anything, binds to `x` |
| `42`, `"hello"` | Literal — matches exact value |
| `'symbol` | Quoted — matches symbol |
| `[a b c]` | Sequence — exact length match |
| `[head .. tail]` | Head-tail — first element + rest |
| `[x y ..]` | Prefix — first N elements, ignore rest |
| `[.. last]` | Suffix — skip to last elements |

```lisp
(match x
  (0 "zero")
  (1 "one")
  (n (string-append "other: " n)))

; Destructuring lists
(match '(1 2 3)
  ([a b c] (+ a (+ b c))))  ; => 6

; Head-tail decomposition
(match '(10 20 30)
  ([head .. tail] head))    ; => 10
```

### 3.7 `and` — Short-circuit And

```lisp
(and expr1 expr2)
```

Returns `expr1` if falsy (without evaluating `expr2`), otherwise returns `expr2`.

```lisp
(and true 42)    ; => 42
(and nil 42)     ; => nil
```

### 3.8 `or` — Short-circuit Or

```lisp
(or expr1 expr2)
```

Returns `expr1` if truthy (without evaluating `expr2`), otherwise returns `expr2`.

```lisp
(or 42 99)     ; => 42
(or nil 99)    ; => 99
```

---

## 4. Path and Index Notation

### 4.1 Dot-Bracket Index Access

Pika Lisp uses dot-bracket notation for collection indexing:

```lisp
collection.[index]
```

The `.` (dot) indicates path/navigation, and `[...]` contains the index expression.

#### List Indexing

```lisp
(define mylist '(10 20 30 40 50))

mylist.[0]      ; => 10
mylist.[2]      ; => 30

(define idx 3)
mylist.[idx]    ; => 40 (variable index)
```

#### String Indexing

```lisp
(define s "hello")

s.[0]           ; => 104 (ASCII code for 'h')
s.[1]           ; => 101 (ASCII code for 'e')
```

#### Chained Indexing

```lisp
matrix.[i].[j]  ; access element at row i, column j
```

### 4.2 Path Notation

Field access using dot notation:

```lisp
point.x             ; field access
person.address.city ; nested field access
```

---

## 5. Primitives

### 5.1 Arithmetic

| Primitive | Arity | Description |
|-----------|-------|-------------|
| `+` | 2 | Addition |
| `-` | 2 | Subtraction |
| `*` | 2 | Multiplication |
| `/` | 2 | Integer division |
| `%` | 2 | Modulo |

```lisp
(+ 3 4)       ; => 7
(- 10 3)      ; => 7
(* 6 7)       ; => 42
(/ 20 4)      ; => 5
(% 17 5)      ; => 2
```

### 5.2 Comparison

| Primitive | Description |
|-----------|-------------|
| `=` | Equality (structural for lists) |
| `<` | Less than |
| `>` | Greater than |
| `<=` | Less than or equal |
| `>=` | Greater than or equal |

```lisp
(= 5 5)       ; => true
(< 3 5)       ; => true
(> 3 5)       ; => nil
```

### 5.3 List Operations

| Primitive | Arity | Description |
|-----------|-------|-------------|
| `cons` | 2 | Construct pair |
| `car` | 1 | First element |
| `cdr` | 1 | Rest of list |
| `list` | variadic | Create list from args |
| `null?` | 1 | Check if nil |
| `pair?` | 1 | Check if cons cell |
| `length` | 1 | List length |

```lisp
(cons 1 2)           ; => (1 . 2)
(cons 1 '(2 3))      ; => (1 2 3)
(car '(1 2 3))       ; => 1
(cdr '(1 2 3))       ; => (2 3)
(null? '())          ; => true
(null? '(1))         ; => nil
```

### 5.4 Boolean

| Primitive | Description |
|-----------|-------------|
| `not` | Logical negation |
| `true` | True symbol |
| `false` | Bound to nil |

```lisp
(not nil)     ; => true
(not true)    ; => nil
(not 0)       ; => nil (0 is truthy)
```

### 5.5 I/O

| Primitive | Description |
|-----------|-------------|
| `print` | Print value (no newline) |
| `println` | Print value with newline |
| `newline` | Print newline |

```lisp
(println "Hello, World!")
(print 42)
(newline)
```

### 5.6 String Primitives

| Primitive | Arity | Description |
|-----------|-------|-------------|
| `string-append` | variadic | Concatenate strings |
| `string-join` | 2 | Join list with separator |
| `substring` | 3 | Extract substring (start, end) |
| `string-split` | 2 | Split by delimiter char |
| `string-length` | 1 | String length |
| `string->list` | 1 | String to list of single-char strings |
| `list->string` | 1 | List of chars to string |
| `string-upcase` | 1 | Convert to uppercase |
| `string-downcase` | 1 | Convert to lowercase |
| `string-trim` | 1 | Remove leading/trailing whitespace |
| `string?` | 1 | Type predicate |

```lisp
(string-append "hello" " " "world")  ; => "hello world"
(string-join ", " '("a" "b" "c"))    ; => "a, b, c"
(substring "hello" 1 4)              ; => "ell"
(string-split "a,b,c" ",")           ; => ("a" "b" "c")
(string-length "hello")              ; => 5
(string-upcase "hello")              ; => "HELLO"
(string-downcase "HELLO")            ; => "hello"
(string-trim "  hi  ")               ; => "hi"
```

**Negative indices:** `substring` supports Python-style negative indices.

```lisp
(substring "hello" 0 -1)   ; => "hell" (all but last)
(substring "hello" -3 -1)  ; => "ll"
```

### 5.7 File I/O Primitives

| Primitive | Arity | Description |
|-----------|-------|-------------|
| `read-file` | 1 | Read entire file as string |
| `write-file` | 2 | Write string to file |
| `file-exists?` | 1 | Check if file exists |
| `read-lines` | 1 | Read file as list of lines |

```lisp
(write-file "test.txt" "Hello\nWorld")  ; => true
(read-file "test.txt")                  ; => "Hello\nWorld"
(file-exists? "test.txt")               ; => true
(read-lines "test.txt")                 ; => ("Hello" "World")
```

### 5.8 Type Predicates

| Primitive | Description |
|-----------|-------------|
| `int?` | Check if integer |
| `string?` | Check if string |
| `symbol?` | Check if symbol |
| `closure?` | Check if closure |
| `continuation?` | Check if continuation |

```lisp
(int? 42)        ; => true
(string? "hi")   ; => true
(closure? +)     ; => nil (+ is a primitive)
```

---

## 6. Delimited Continuations

Pika Lisp provides first-class delimited continuations via `reset` and `shift`.

### 6.1 `reset` — Establish Delimiter

```lisp
(reset body)
```

Establishes a delimiter (prompt) for continuation capture.

### 6.2 `shift` — Capture Continuation

```lisp
(shift k body)
```

Captures the continuation up to the enclosing `reset` and binds it to `k`.

```lisp
(reset
  (+ 1 (shift k (k (k 10)))))
; => 12
; Explanation: k = (lambda (x) (+ 1 x))
; (k (k 10)) = (+ 1 (+ 1 10)) = 12
```

### 6.3 Continuation Semantics

- Continuations are **one-shot** by default
- The continuation `k` is a function: `(k value)` resumes with `value`
- The result of `shift` body becomes the result of `reset`

---

## 7. Effect Handlers

Effect handlers provide structured control for algebraic effects.

### 7.1 `perform` — Signal Effect

```lisp
(perform effect-tag argument)
```

Signals an effect with the given tag and argument.

### 7.2 `handle` — Install Handler

```lisp
(handle body
  ((effect-tag k arg) handler-body)
  ...)
```

Evaluates `body` with effect handlers installed. When an effect is performed:
- `k` is bound to the continuation
- `arg` is bound to the effect argument
- `handler-body` is evaluated

```lisp
(handle
  (+ 1 (perform read nil))
  ((read k x) (k 41)))
; => 42
; The handler provides 41 as the value for (perform read nil)
```

---

## 8. REPL

Launch the interactive REPL with:

```bash
./build/main --repl
# or
./build/main -repl
```

### REPL Commands

| Command | Description |
|---------|-------------|
| `quit` | Exit the REPL |
| `exit` | Exit the REPL |

### REPL Features

- Immediate expression evaluation
- Error messages with source location (line, column)
- Persistent global environment across expressions

```
Lisp REPL (type 'quit' or 'exit' to leave)
---
> (define x 10)
10
> (+ x 5)
15
> (define inc (lambda (n) (+ n 1)))
#<closure>
> (inc x)
11
> quit
Goodbye!
```

---

## 9. Examples

### 9.1 Factorial

```lisp
(define fact
  (lambda (n)
    (if (= n 0)
        1
        (* n (fact (- n 1))))))

(fact 5)  ; => 120
```

### 9.2 Map Function

```lisp
(define map
  (lambda (f)
    (lambda (lst)
      (if (null? lst)
          '()
          (cons (f (car lst))
                ((map f) (cdr lst)))))))

((map (lambda (x) (* x 2))) '(1 2 3))  ; => (2 4 6)
```

### 9.3 List Indexing with Dot-Bracket

```lisp
(define data '(10 20 30 40 50))

; Access by literal index
data.[0]    ; => 10
data.[2]    ; => 30

; Access by variable
(define i 4)
data.[i]    ; => 50

; Nested lists
(define matrix '((1 2 3) (4 5 6) (7 8 9)))
matrix.[0].[0]   ; => 1
matrix.[1].[2]   ; => 6
```

### 9.4 File Processing

```lisp
; Read a file and count lines
(define count-lines
  (lambda (path)
    (let ((lines (read-lines path)))
      (length lines))))

; Write uppercase version of file
(define upcase-file
  (lambda (in-path)
    (lambda (out-path)
      (let ((content (read-file in-path)))
        (write-file out-path (string-upcase content))))))
```

### 9.5 Effect Handler Example

```lisp
; Simple state effect
(handle
  (let ((x (perform get nil)))
    (perform put (+ x 1))
    (perform get nil))
  ((get k _) (k 0))        ; initial state is 0
  ((put k v) (k nil)))     ; ignore puts for now
; => 0 (simplified - full state needs threading)
```

---

## Appendix A: Grammar (EBNF)

```ebnf
program     = { expr } ;
expr        = literal | symbol | path | quoted | list | indexed ;

literal     = integer | string ;
integer     = [ "-" ] digit { digit } ;
string      = '"' { char | escape } '"' ;
symbol      = symbol_char { symbol_char } ;
path        = symbol "." symbol { "." symbol } ;

quoted      = "'" datum ;
list        = "(" { expr } ")" ;
indexed     = expr ".[" expr "]" ;

datum       = literal | symbol | "(" { datum } ")" | "'" datum ;

symbol_char = letter | digit | "_" | "-" | "+" | "*" | "/"
            | "=" | "<" | ">" | "!" | "?" | ":" | "@" | "#"
            | "$" | "%" | "&" | "|" | "^" | "~" ;
```

---

## Appendix B: Implementation Notes

### Memory Model

Pika Lisp runs on a region-based memory system:
- Objects are allocated in regions
- Regions form a tree hierarchy
- When a region dies, objects are promoted to parent
- Handles remain valid via forwarding pointers

### Currying

All functions are curried. Multi-argument applications:
```lisp
(f a b c)  ; desugars to (((f a) b) c)
```

Binary primitives like `+` return a partial application when given one argument.

### Limitations

- Maximum symbol/string length: 64 characters
- Maximum symbols: 256
- Maximum values: 4096
- Single-threaded evaluation
- No garbage collection (region-based cleanup)

---

*Pika Lisp — A minimal Lisp with continuations and effects*
