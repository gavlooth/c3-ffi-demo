# Omni Lisp Language Specification

**Version:** 0.4.0
**Date:** 2026-02-20

Omni Lisp is a Lisp dialect with first-class delimited continuations, algebraic effects, auto-curried lambdas, multiple dispatch, and a structural type system. It runs on a region-based memory system implemented in C3 with three backends: interpreter, GNU Lightning JIT, and Lisp-to-C3 transpiler.

---

## Table of Contents

1. [Syntax Overview](#1-syntax-overview)
2. [Data Types](#2-data-types)
3. [Special Forms](#3-special-forms)
4. [Type System](#4-type-system)
5. [Multiple Dispatch](#5-multiple-dispatch)
6. [Path and Index Notation](#6-path-and-index-notation)
7. [Primitives](#7-primitives)
8. [Standard Library](#8-standard-library)
9. [Delimited Continuations](#9-delimited-continuations)
10. [Effect Handlers](#10-effect-handlers)
11. [Macros](#11-macros)
12. [Modules](#12-modules)
13. [REPL](#13-repl)
14. [Examples](#14-examples)

---

## 1. Syntax Overview

### 1.1 Lexical Elements

```
; Comments start with semicolon and extend to end of line

; Integers
42
-17
0

; Floating-point numbers
3.14
-0.5
1.0

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

; Collection literals
[1 2 3]         ; array literal, desugars to (array 1 2 3)
{'a 1 'b 2}     ; dict literal, desugars to (dict 'a 1 'b 2)

; Quote shorthand
'symbol     ; equivalent to (quote symbol)
'(a b c)    ; equivalent to (quote (a b c))

; Quasiquote
`(a ,x ,@xs)   ; template with unquote and splicing
```

### 1.2 S-Expression Forms

```lisp
; Function application (auto-curried)
(f arg1 arg2 ...)     ; desugars to (((f arg1) arg2) ...)

; Examples
(+ 1 2)              ; adds 1 and 2
(map inc '(1 2 3))   ; applies inc to each element

; Empty list / nil
()
```

### 1.3 Special Tokens

| Token | Description |
|-------|-------------|
| `_` | Wildcard (in patterns), NOT a symbol |
| `..` | Rest/spread in patterns and variadic params |
| `.[` | Dot-bracket for index access |
| `.` | Dot for field/path access |
| `^` | Type annotation prefix |
| `[` `]` | Array literals, bracket attributes, and patterns |
| `{` `}` | Dict literals |

---

## 2. Data Types

### 2.1 Core Types

| Type | Tag | Description | Example |
|------|-----|-------------|---------|
| nil | `NIL` | Empty/false value | `nil`, `()` |
| int | `INT` | 64-bit signed integer | `42`, `-17` |
| double | `DOUBLE` | 64-bit floating point | `3.14`, `-0.5` |
| string | `STRING` | Immutable string (max 4095 chars) | `"hello"` |
| symbol | `SYMBOL` | Interned identifier | `'foo`, `'hello` |
| cons | `CONS` | Pair / list cell | `(cons 1 2)`, `'(1 2 3)` |
| closure | `CLOSURE` | User-defined function with environment | `(lambda (x) x)` |
| continuation | `CONTINUATION` | Captured delimited continuation | via `shift` |
| primitive | `PRIMITIVE` | Built-in function | `+`, `car` |
| partial | `PARTIAL_PRIM` | Partially applied primitive | `(+ 3)` |
| error | `ERROR` | Error value | `(error "oops")` |
| dict | `HASHMAP` | Mutable hash table | `{'a 1}`, `(dict 'a 1)` |
| array | `ARRAY` | Mutable dynamic array | `[1 2 3]`, `(array 1 2 3)` |
| ffi-handle | `FFI_HANDLE` | Foreign library handle | `(ffi-open "libc.so.6")` |
| instance | `INSTANCE` | User-defined type instance | `(Point 3 4)` |
| method-table | `METHOD_TABLE` | Multiple dispatch table | internal |

### 2.2 Truthiness

- **Falsy:** `nil`, `false`
- **Truthy:** Everything else, including `0`, `""`, `'()`, and empty collections

### 2.3 Equality

`=` performs structural equality:
- Integers and doubles: numeric comparison
- Strings: character-by-character
- Symbols: identity (interned)
- Lists: recursive structural equality
- Other types: identity

---

## 3. Special Forms

### 3.1 `lambda` -- Function Definition

```lisp
; Single parameter
(lambda (x) body)

; Multi-parameter (auto-curried by parser)
(lambda (x y z) body)
; desugars to: (lambda (x) (lambda (y) (lambda (z) body)))

; Zero-argument
(lambda () body)

; Variadic
(lambda (x .. rest) body)

; Typed parameters (for dispatch, NOT curried)
(lambda ((^Int x) (^String y)) body)
```

### 3.2 `define` -- Global Definition

```lisp
; Simple define
(define name value)

; Shorthand function define
(define (f x y) body)
; desugars to: (define f (lambda (x y) body))

; Zero-arg shorthand
(define (thunk) 42)

; Typed function define (creates dispatch entry)
(define (describe (^Int n)) "integer")
(define (describe (^String s)) "string")
(define (describe x) "other")   ; fallback
```

### 3.3 `let` -- Local Binding

```lisp
; Simple let
(let ((name value)) body)

; Multi-binding (desugars to nested lets)
(let ((x 1) (y 2)) (+ x y))

; Recursive let
(let ^rec ((fact (lambda (n) (if (= n 0) 1 (* n (fact (- n 1)))))))
  (fact 5))

; Named let (loop construct)
(let loop ((n 5) (acc 1))
  (if (= n 0) acc
      (loop (- n 1) (* acc n))))
; Named let desugars to let ^rec
```

### 3.4 `if` -- Conditional

```lisp
(if test then-expr else-expr)
```

Three branches required. Only the chosen branch is evaluated.

### 3.5 `begin` -- Sequencing

```lisp
(begin e1 e2 ... en)
```

Evaluates all expressions in order, returns the last. Last expression is in tail position (TCO).

### 3.6 `set!` -- Mutation

```lisp
(set! name value)              ; variable mutation
(set! instance.field value)    ; struct field mutation
(set! obj.nested.field value)  ; nested field mutation
(set! pair.car value)          ; cons cell car mutation
(set! pair.cdr value)          ; cons cell cdr mutation
```

### 3.7 `quote` / `quasiquote`

```lisp
(quote datum)       ; or 'datum
'foo                ; => symbol foo
'(1 2 3)            ; => list (1 2 3)

`(a ,(+ 1 2) ,@(list 3 4))  ; => (a 3 3 4)
```

Quasiquote supports nesting with depth tracking (Bawden's algorithm).

### 3.8 `and` / `or` -- Short-Circuit Logic

```lisp
(and left right)    ; returns left if falsy, else right
(or left right)     ; returns left if truthy, else right
```

### 3.9 `match` -- Pattern Matching

```lisp
(match expr
  (pattern1 result1)
  (pattern2 result2)
  (_ default))
```

Up to 16 clauses. Pattern types:

| Pattern | Description | Example |
|---------|-------------|---------|
| `_` | Wildcard | `(_ "default")` |
| `x` | Variable binding | `(n (* n 2))` |
| `42` | Integer literal | `(0 "zero")` |
| `"hi"` | String literal | `("hi" "greeting")` |
| `'sym` | Quoted symbol | `('red "red")` |
| `[a b c]` | Exact sequence | `([x y] (+ x y))` |
| `[h .. t]` | Head-tail | `([first .. rest] first)` |
| `[x y ..]` | Prefix | `([a b ..] (+ a b))` |
| `[.. last]` | Suffix | `([.. z] z)` |
| `None` | Nullary constructor | `(None "empty")` |
| `(Some x)` | Constructor pattern | `((Some v) v)` |

---

## 4. Type System

### 4.1 Struct Types

```lisp
(define [type] Point (^Int x) (^Int y))

(Point 3 4)        ; construction
point.x             ; field access => 3
point.[0]           ; positional access => 3
(set! point.x 99)   ; field mutation
```

### 4.2 Type Inheritance

```lisp
(define [abstract] Shape)
(define [type] (Circle Shape) (^Int radius))

(is? (Circle 5) 'Shape)   ; => true (subtype check)
(is? (Circle 5) 'Circle)  ; => true
```

Syntax: `(define [type] (ChildName ParentName) fields...)` for inheritance.

### 4.3 Union Types (Sum Types / ADTs)

```lisp
(define [union] (Option T) None (Some T))
(define [union] (Result T E) (Ok T) (Err E))

; Construction
None                    ; nullary variant
(Some 42)               ; variant with value

; Pattern matching
(match opt
  (None "empty")
  ((Some x) x))
```

### 4.4 Type Aliases

```lisp
(define [alias] Num Int)
```

### 4.5 Type Annotations

```lisp
^Int                    ; simple type
^(List Int)             ; compound type
^(Val 42)               ; value-level type (match literal)
```

### 4.6 Type Introspection

```lisp
(type-of 42)            ; => 'Int
(type-of "hi")          ; => 'String
(type-of (Point 1 2))   ; => 'Point
(is? 42 'Int)           ; => true
(is? (Circle 5) 'Shape) ; => true (walks parent chain)
(instance? (Point 1 2)) ; => true
(instance? 42)          ; => nil
```

---

## 5. Multiple Dispatch

### 5.1 Basic Dispatch

Define multiple implementations with typed parameters. Best match wins:

```lisp
(define (describe (^Int n)) "integer")
(define (describe (^String s)) "string")
(define (describe x) "other")

(describe 42)       ; => "integer"
(describe "hi")     ; => "string"
(describe '(1 2))   ; => "other"
```

### 5.2 Multi-Argument Dispatch

```lisp
(define (add2 (^Int a) (^Int b)) (+ a b))
(define (add2 (^String a) (^String b)) (string-append a b))

(add2 3 4)              ; => 7
(add2 "hello" " world") ; => "hello world"
```

### 5.3 Val Dispatch (Value-Level Matching)

```lisp
(define (fib (^(Val 0) n)) 0)
(define (fib (^(Val 1) n)) 1)
(define (fib (^Int n)) (+ (fib (- n 1)) (fib (- n 2))))

(fib 10)    ; => 55
```

### 5.4 Dispatch Scoring

| Match Type | Score | Description |
|------------|-------|-------------|
| Val literal | 1000 | `^(Val 42)` matches value 42 |
| Exact type | 100 | `^Int` matches INT value |
| Subtype | 10 | `^Shape` matches Circle (Shape child) |
| Any type | 1 | Untyped parameter matches anything |

Highest-scoring method wins. Ties broken by first-registered.

---

## 6. Path and Index Notation

### 6.1 Dot-Bracket Index Access

```lisp
list.[0]            ; first element
str.[2]             ; character code at index 2
matrix.[i].[j]      ; chained indexing
array.[0]           ; array indexing
dict.['key]         ; dict key lookup
```

### 6.2 Path Notation (Field Access)

```lisp
point.x             ; struct field access
line.start.y        ; nested field access (up to 8 segments)
pair.car             ; cons cell car access
pair.cdr             ; cons cell cdr access
```

For non-Instance values, path notation uses alist lookup (association lists). Cons cells also support `.car` and `.cdr` as special field names.

---

## 7. Primitives

### 7.1 Arithmetic (5)

| Prim | Arity | Description |
|------|-------|-------------|
| `+` | 2 | Addition (int or double) |
| `-` | 2 | Subtraction |
| `*` | 2 | Multiplication |
| `/` | 2 | Integer/float division |
| `%` | 2 | Modulo |

Binary primitives auto-curry: `(+ 3)` returns a function that adds 3.

### 7.2 Comparison (5)

| Prim | Description |
|------|-------------|
| `=` | Structural equality |
| `<` | Less than |
| `>` | Greater than |
| `<=` | Less or equal |
| `>=` | Greater or equal |

### 7.3 List Operations (7)

| Prim | Arity | Description |
|------|-------|-------------|
| `cons` | 2 | Construct pair |
| `car` | 1 | First element |
| `cdr` | 1 | Rest element |
| `list` | variadic | Create list; `(list [1 2 3])` converts array to list |
| `length` | 1 | Generic: list, array, dict, or string length |
| `null?` | 1 | Check if nil |
| `pair?` | 1 | Check if cons |

### 7.4 Boolean (1)

| Prim | Description |
|------|-------------|
| `not` | Logical negation |

### 7.5 I/O (4, via effects)

| Prim | Description |
|------|-------------|
| `print` | Output value (no newline) |
| `println` | Output value with newline |
| `display` | Display value |
| `newline` | Output newline |

I/O primitives go through algebraic effects (`io/print`, `io/println`, etc.). When no handler is installed, a fast path calls raw primitives directly (zero overhead). Custom handlers can intercept, suppress, or redirect I/O.

### 7.6 String Operations (15)

| Prim | Arity | Description |
|------|-------|-------------|
| `string-append` | variadic | Concatenate strings |
| `string-join` | 2 | Join list with separator |
| `substring` | 3 | Extract substring (negative indices supported) |
| `string-split` | 2 | Split by delimiter |
| `string-length` | 1 | String length |
| `string->list` | 1 | String to list of chars |
| `list->string` | 1 | List to string |
| `string-upcase` | 1 | Uppercase |
| `string-downcase` | 1 | Lowercase |
| `string-trim` | 1 | Trim whitespace |
| `string-contains?` | 2 | Substring search |
| `string-index-of` | 2 | Find index of substring |
| `string-replace` | 3 | Replace occurrences |
| `char-at` | 2 | Character at index |
| `string-repeat` | 2 | Repeat string N times |

### 7.7 Type Predicates (12)

| Prim | Description |
|------|-------------|
| `string?` | Is string? |
| `int?` | Is integer? |
| `double?` | Is double? |
| `number?` | Is int or double? |
| `symbol?` | Is symbol? |
| `closure?` | Is closure? |
| `continuation?` | Is continuation? |
| `boolean?` | Is true or false? |
| `list?` | Is proper list? |
| `procedure?` | Is callable? |
| `dict?` | Is dict? |
| `array?` | Is array? |

### 7.8 Numeric Predicates (4)

| Prim | Description |
|------|-------------|
| `zero?` | Is zero? |
| `positive?` | Is positive? |
| `negative?` | Is negative? |
| `even?` / `odd?` | Parity check |

### 7.9 File I/O (5, via effects)

| Prim | Arity | Description |
|------|-------|-------------|
| `read-file` | 1 | Read file as string |
| `write-file` | 2 | Write string to file |
| `file-exists?` | 1 | Check file existence |
| `read-lines` | 1 | Read file as list of lines |
| `load` | 1 | Load and evaluate a .omni file |

### 7.10 Dict Operations (2)

| Prim | Arity | Description |
|------|-------|-------------|
| `dict` | variadic | Create dict from key-value pairs; `{'a 1 'b 2}` desugars to this |
| `dict-set!` | 3 | Set key-value pair |

### 7.11 Array Operations (2)

| Prim | Arity | Description |
|------|-------|-------------|
| `array` | variadic | Create array; `[1 2 3]` desugars to this; `(array '(1 2 3))` converts list to array |
| `array-set!` | 3 | Set element at index |

### 7.12 Generic Collection Operations (6)

| Prim | Arity | Description | Supported types |
|------|-------|-------------|-----------------|
| `ref` | 2 | Lookup by key/index | array (int), dict (any), cons (0=car, 1=cdr), string (char) |
| `push!` | 2 | Append element | array |
| `keys` | 1 | List of keys | dict |
| `values` | 1 | List of values | dict |
| `has?` | 2 | Check key existence | dict |
| `remove!` | 2 | Remove by key | dict |

Note: `length` (Section 7.3) is also generic — works on lists, arrays, dicts, and strings.

### 7.13 Set Operations (5)

| Prim | Arity | Description |
|------|-------|-------------|
| `set` | variadic | Create set |
| `set-add` | 2 | Add element |
| `set-remove` | 2 | Remove element |
| `set-contains?` | 2 | Check membership |
| `set-size` | 1 | Set cardinality |

### 7.14 Math Library (19)

| Prim | Description |
|------|-------------|
| `sin`, `cos`, `tan` | Trigonometric |
| `asin`, `acos`, `atan` | Inverse trig |
| `atan2` | Two-argument arctangent |
| `exp`, `log`, `log10` | Exponential/logarithmic |
| `pow`, `sqrt` | Power/root |
| `floor`, `ceiling`, `round`, `truncate` | Rounding |
| `abs` | Absolute value |
| `min`, `max` | Binary min/max |
| `gcd`, `lcm` | Number theory |

### 7.15 Bitwise Operations (6)

| Prim | Description |
|------|-------------|
| `bitwise-and`, `bitwise-or`, `bitwise-xor` | Bitwise logic |
| `bitwise-not` | Bitwise complement |
| `lshift`, `rshift` | Bit shifting |

### 7.16 Conversion (6)

| Prim | Description |
|------|-------------|
| `string->number` | Parse string to number |
| `number->string` | Number to string |
| `exact->inexact` | Int to double |
| `inexact->exact` | Double to int |
| `string->symbol` | String to symbol |
| `symbol->string` | Symbol to string |

### 7.17 Introspection & Meta (7)

| Prim | Description |
|------|-------------|
| `type-of` | Type name as symbol |
| `is?` | Type/subtype check |
| `instance?` | Check if type instance |
| `eval` | Evaluate expression |
| `apply` | Apply function to arg list |
| `macroexpand` | Expand macro |
| `bound?` | Check if name is defined |

### 7.18 Error Handling (2)

| Prim | Description |
|------|-------------|
| `error` | Create error value |
| `error-message` | Extract message from error |

### 7.19 Miscellaneous (5)

| Prim | Description |
|------|-------------|
| `gensym` | Generate unique symbol |
| `format` | Format string with values |
| `sort` | Sort list |
| `sort-by` | Sort list by comparator |
| `read-string` | Parse string to Lisp value |

### 7.20 FFI (4)

| Prim | Arity | Description |
|------|-------|-------------|
| `ffi-open` | 1 | Open shared library via dlopen |
| `ffi-call` | variadic | Call foreign function with type annotations |
| `ffi-close` | 1 | Close library handle |
| `ffi-sym` | 2 | Get function pointer as integer |

```lisp
(define libc (ffi-open "libc.so.6"))
(ffi-call libc "strlen" 'size "hello" 'string)  ; => 5
(ffi-call libc "abs" 'int -42 'int)             ; => 42
(ffi-close libc)
```

FFI type symbols: `'int`, `'size`, `'string`, `'void`, `'ptr`, `'double`

### 7.21 Constants

| Name | Value |
|------|-------|
| `true` | Symbol `true` |
| `false` | Bound to `nil` |
| `pi` | 3.141592653589793 |
| `e` | 2.718281828459045 |

**Total: 129+ primitives**

---

## 8. Standard Library

Higher-order functions and utilities defined in Omni:

| Function | Signature | Description |
|----------|-----------|-------------|
| `map` | `(f lst)` | Apply f to each element |
| `filter` | `(pred lst)` | Keep elements matching predicate |
| `foldl` | `(f acc lst)` | Left fold |
| `foldr` | `(f init lst)` | Right fold |
| `append` | `(a b)` | Concatenate lists |
| `reverse` | `(lst)` | Reverse list |
| `compose` | `(f g)` | Function composition |
| `id` | `(x)` | Identity function |
| `nth` | `(n lst)` | Nth element |
| `take` | `(n lst)` | First N elements |
| `drop` | `(n lst)` | Drop first N elements |
| `zip` | `(a b)` | Zip two lists |
| `range` | `(n)` | List from 0 to n-1 |
| `for-each` | `(f lst)` | Apply f for side effects |
| `any?` | `(pred lst)` | Any element matches? |
| `every?` | `(pred lst)` | All elements match? |
| `flatten` | `(lst)` | Flatten nested list (1 level) |
| `partition` | `(pred lst)` | Split by predicate |
| `remove` | `(pred lst)` | Remove matching elements |
| `find` | `(pred lst)` | First matching element |
| `assoc` | `(key alist)` | Association list lookup |
| `assoc-ref` | `(key alist)` | Lookup value only |

All stdlib functions are curried (e.g., `(map inc)` returns a function).

### 8.1 Macros

| Macro | Description |
|-------|-------------|
| `when` | `(when test body...)` -- if test, evaluate body |
| `unless` | `(unless test body...)` -- if not test, evaluate body |
| `cond` | `(cond (t1 b1) (t2 b2) ...)` -- multi-branch conditional |

### 8.2 Effect Utilities

| Name | Description |
|------|-------------|
| `try` | `(try thunk handler)` -- catch `raise` effects |
| `assert!` | `(assert! cond msg)` -- raise if condition fails |
| `yield` | Macro for generator-style values |
| `stream-take` | Take N values from a generator stream |

### 8.3 Lazy Evaluation

| Name | Description |
|------|-------------|
| `delay` | `(delay thunk)` -- create lazy value (memoized) |
| `force` | `(force promise)` -- force lazy evaluation |

---

## 9. Delimited Continuations

### 9.1 `reset` -- Establish Delimiter

```lisp
(reset body)
```

### 9.2 `shift` -- Capture Continuation

```lisp
(shift k body)
```

Captures the continuation up to the enclosing `reset` and binds it to `k`.

```lisp
(reset (+ 1 (shift k (k (k 10)))))
; k = (lambda (x) (+ 1 x))
; (k (k 10)) = (+ 1 (+ 1 10)) = 12
```

### 9.3 Semantics

- Continuations are **one-shot** by default
- `k` is a function: `(k value)` resumes with `value`
- The result of `shift`'s body becomes the result of `reset`

---

## 10. Effect Handlers

### 10.1 `perform` -- Signal Effect

```lisp
(perform effect-tag argument)
```

### 10.2 `handle` -- Install Handler

```lisp
(handle body
  ((effect-tag k arg) handler-body)
  ...)
```

When an effect is performed:
- `k` is the continuation
- `arg` is the effect argument
- `handler-body` can resume with `(k value)` or abort

```lisp
(handle
  (+ 1 (perform read nil))
  ((read k x) (k 41)))
; => 42
```

### 10.3 I/O Effects

I/O operations go through effects with a fast path:

```lisp
; These use io/print, io/println, etc. effect tags
(println "hello")     ; fast path when no handler
(print 42)

; Custom handler intercepts I/O
(handle (begin (println "suppressed") 42)
  ((io/println k x) (k nil)))
; => 42 (output suppressed)

; Capture output
(handle (begin (println "captured") nil)
  ((io/println k x) x))
; => "captured"
```

Effect tags: `io/print`, `io/println`, `io/display`, `io/newline`, `io/read-file`, `io/write-file`, `io/file-exists?`, `io/read-lines`

---

## 11. Macros

### 11.1 Pattern-Based Macros

```lisp
(define [macro] when
  ([test .. body] (if test (begin .. body) nil)))

(define [macro] cond
  ([] nil)
  ([test body .. rest] (if test body (cond .. rest))))
```

- Pattern-based with template substitution
- Hygienic: template literals resolve at definition time
- Auto-gensym: `name#` in templates generates unique symbols
- `gensym` function for manual hygiene
- Up to 8 clauses per macro

### 11.2 Expansion

```lisp
(macroexpand '(when true 1 2 3))
; => (if true (begin 1 2 3) nil)
```

---

## 12. Modules

```lisp
(module math-utils (export add multiply)
  (define (add a b) (+ a b))
  (define (multiply a b) (* a b)))

(import math-utils)
(add 3 4)  ; => 7
```

- Modules define namespaces with explicit exports
- `import` brings exported symbols into scope
- File-based import: `(import "path/to/file.omni")`
- Cached: modules loaded only once
- Circular import detection

---

## 13. REPL

```bash
./build/main --repl    # or -repl
```

```
Lisp REPL (type 'quit' or 'exit' to leave)
---
> (define x 10)
10
> (+ x 5)
15
> (define (inc n) (+ n 1))
#<closure>
> (inc x)
11
> quit
Goodbye!
```

---

## 14. Examples

### 14.1 Factorial

```lisp
(define (fact n)
  (if (= n 0) 1
      (* n (fact (- n 1)))))
(fact 10)  ; => 3628800
```

### 14.2 Fibonacci with Dispatch

```lisp
(define (fib (^(Val 0) n)) 0)
(define (fib (^(Val 1) n)) 1)
(define (fib (^Int n)) (+ (fib (- n 1)) (fib (- n 2))))
(fib 10)  ; => 55
```

### 14.3 Option Type

```lisp
(define [union] (Option T) None (Some T))

(define (safe-div a b)
  (if (= b 0) None (Some (/ a b))))

(match (safe-div 10 3)
  (None "division by zero")
  ((Some x) x))
; => 3
```

### 14.4 Effect Handler for State

```lisp
(handle
  (let ((x (perform get nil)))
    (begin
      (perform put (+ x 1))
      (perform get nil)))
  ((get k _) (k 0))
  ((put k v) (k nil)))
```

### 14.5 Collection Literals and Generic Operations

```lisp
; Array literal
(define nums [1 2 3 4 5])
(ref nums 0)           ; => 1
(length nums)           ; => 5
(push! nums 6)          ; mutates, adds 6

; Dict literal
(define person {'name "Alice" 'age 30})
(ref person 'name)      ; => "Alice"
(has? person 'age)      ; => true
(keys person)           ; => '(name age)

; Constructor dispatch
(array '(1 2 3))        ; list → array conversion
(list [1 2 3])          ; array → list conversion

; Cons mutation via dot-path
(define p (cons 1 2))
(set! p.car 99)
p.car                   ; => 99
```

### 14.6 Type Hierarchy

```lisp
(define [abstract] Shape)
(define [type] (Circle Shape) (^Int radius))
(define [type] (Rect Shape) (^Int width) (^Int height))

(define (area (^Circle c)) (* pi (* c.radius c.radius)))
(define (area (^Rect r)) (* r.width r.height))

(area (Circle 5))      ; => ~78.5
(area (Rect 3 4))      ; => 12
```

---

## Appendix A: Grammar (EBNF)

```ebnf
program     = { expr } ;
expr        = literal | symbol | path | quoted | quasiquoted
            | list | array_lit | dict_lit | indexed ;

literal     = integer | float | string ;
integer     = [ "-" ] digit { digit } ;
float       = [ "-" ] digit { digit } "." digit { digit } ;
string      = '"' { char | escape } '"' ;
symbol      = symbol_char { symbol_char } ;
path        = symbol "." symbol { "." symbol } ;

quoted      = "'" datum ;
quasiquoted = "`" datum ;
list        = "(" { expr } ")" ;
array_lit   = "[" { expr } "]" ;           (* desugars to (array ...) *)
dict_lit    = "{" { expr expr } "}" ;      (* desugars to (dict ...), must be even *)
indexed     = expr ".[" expr "]" ;

datum       = literal | symbol | "(" { datum } ")" | "'" datum ;

symbol_char = letter | digit | "_" | "-" | "+" | "*" | "/"
            | "=" | "<" | ">" | "!" | "?" | ":" | "@" | "#"
            | "$" | "%" | "&" | "|" | "^" | "~" ;
```

---

## Appendix B: Limits

| Resource | Limit |
|----------|-------|
| Symbol/string length | 4095 characters |
| Total symbols | 4096 |
| Bindings per env frame | 512 |
| Match clauses | 16 |
| Pattern elements | 16 |
| Effect handler clauses | 8 |
| Handler stack depth | 16 |
| Call arguments | 64 |
| Path segments | 8 |
| Begin expressions | 64 |
| Lambda params | 64 |
| Macros | 64 |
| Macro clauses | 8 |
| Modules | 32 |
| Module exports | 128 |
| Eval depth | 200 |
| Registered types | 128 |
| Type fields | 16 |
| Method table entries | 32 |

---

## Appendix C: Backends

| Feature | Interpreter | JIT | Compiler |
|---------|:-----------:|:---:|:--------:|
| lambda/define/let/if | Y | Y | Y |
| begin/set!/and/or | Y | Y | Y |
| quote/quasiquote | Y | Y | Y |
| match | Y | Y | Y |
| reset/shift | Y | eval* | eval* |
| handle/perform | Y | eval* | eval* |
| type definitions | Y | eval* | eval* |
| dispatch | Y | eval* | eval* |
| macros | Y | Y** | Y** |
| modules | Y | eval* | eval* |

*eval* = delegates to interpreter
**Y** = macro expansion at parse time

---

*Omni Lisp -- A Lisp with delimited continuations, algebraic effects, auto-curried lambdas, multiple dispatch, and structural types*
