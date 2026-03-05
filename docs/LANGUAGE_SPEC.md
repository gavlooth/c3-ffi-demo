# Omni Lisp Language Specification

**Version:** 0.4.5
**Date:** 2026-02-28

Omni Lisp is a Lisp dialect with first-class delimited continuations, algebraic effects, strict-arity multi-param lambdas, multiple dispatch, and a structural type system. It runs on a scope-region memory system (arena-per-call with reference-counted closures) implemented in C3 with a GNU Lightning JIT engine and a Lisp-to-C3 AOT transpiler.

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
15. [CLI & Project Tooling](#15-cli--project-tooling)

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
[1 2 3]         ; array literal, desugars to (array 1 2 3) (Array alias available)
{'a 1 'b 2}     ; dict literal, desugars to (dict 'a 1 'b 2) (Dict alias available)

; Quote shorthand
'symbol     ; equivalent to (quote symbol)
'(a b c)    ; equivalent to (quote (a b c))

; Quasiquote
`(a ,x ,@xs)   ; template with unquote and splicing
```

### 1.2 S-Expression Forms

```lisp
; Function application
(f arg1 arg2 ...)     ; multi-arg call (strict arity)

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
| string | `STRING` | Immutable string (heap-allocated) | `"hello"` |
| symbol | `SYMBOL` | Interned identifier | `'foo`, `'hello` |
| cons | `CONS` | Pair / list cell | `(cons 1 2)`, `'(1 2 3)` |
| closure | `CLOSURE` | User-defined function with environment | `(lambda (x) x)` |
| continuation | `CONTINUATION` | Captured delimited continuation | via `shift` |
| primitive | `PRIMITIVE` | Built-in function | `+`, `car` |
| partial | `PARTIAL_PRIM` | Partially applied primitive | `(+ 3)` |
| error | `ERROR` | Error value | `(error "oops")` |
| dict | `HASHMAP` | Mutable hash table | `{'a 1}`, `(dict 'a 1)` |
| array | `ARRAY` | Mutable dynamic array | `[1 2 3]`, `(array 1 2 3)` |
| coroutine | `COROUTINE` | User-level coroutine | `(coroutine (lambda () body))` |
| ffi-handle | `FFI_HANDLE` | Foreign library handle | `(define [ffi lib] libc "libc.so.6")` |
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

; Multi-parameter (strict arity)
(lambda (x y z) body)
; requires exactly 3 arguments — use _ placeholder, |> pipe, or partial for partial application

; Zero-argument
(lambda () body)

; Variadic
(lambda (x .. rest) body)

; Typed parameters (for dispatch)
(lambda ((^Int x) (^String y)) body)

; Dict destructuring parameter — caller passes a dict
(lambda ({name age}) (println name age))
; called as: (f {'name "Alice" 'age 30})

; Mixed positional + dict params
(lambda ({host port} verbose) body)
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

; Dict destructuring parameter
(define (connect {host port timeout}) (tcp-connect host port))
; called as: (connect {'host "localhost" 'port 8080 'timeout 5000})

; Mixed dict + positional params
(define (request {method url} body) ...)
```

Note: `(define [...] ...)` with brackets is reserved for attribute syntax (`[type]`, `[ffi lib]`, `[relation db]`, etc.). Array destructuring is only available in `let` and `match`.

### 3.3 `let` -- Local Binding

```lisp
; Simple let (flat pairs)
(let (name value) body)

; Multi-binding (desugars to nested lets)
(let (x 1 y 2) (+ x y))

; Array destructuring
(let ([x y] [10 20]) (+ x y))         ; => 30
(let ([head .. tail] '(1 2 3)) head)   ; => 1
(let ([a b ..] '(1 2 3 4 5)) (+ a b)) ; => 3

; Dict destructuring
(let ({name age} {'name "Alice" 'age 30}) name) ; => "Alice"
(let ({x y} {'x 10 'y 20}) (+ x y))             ; => 30

; Mixed bindings (plain + destructuring)
(let ([a b] [3 4] z 5) (+ a (+ b z)))  ; => 12

; Recursive let
(let ^rec (fact (lambda (n) (if (= n 0) 1 (* n (fact (- n 1))))))
  (fact 5))

; Named let (loop construct)
(let loop (n 5 acc 1)
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

Dynamic clause count (no fixed limit). Pattern types:

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
(typeof 42)             ; => 'Int  (alias)
(type-of "hi")          ; => 'String
(type-of (Point 1 2))   ; => 'Point
(= (type-of (Array 1 2)) 'Array) ; exact type via symbol equality
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
| `-` | 1-2 | Subtraction; `(- n)` negates |
| `*` | 2 | Multiplication |
| `/` | 2 | Integer/float division |
| `%` | 2 | Modulo |

Binary primitives partially apply when given one argument: `(+ 3)` returns a `PARTIAL_PRIM` that adds 3. This is built-in for binary primitives only — user-defined lambdas have strict arity (see `_` placeholder, `|>` pipe, or `partial` for general partial application).

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
| `list` / `List` | variadic | Create list; single collection arg dispatches conversion (`(List [1 2 3])`, `(List (iterator ...))`) |
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
| `dict` / `Dict` | variadic | Create dict from key-value pairs; `{'a 1 'b 2}` desugars to this |
| `dict-set!` | 3 | Set key-value pair |

### 7.11 Array Operations (2)

| Prim | Arity | Description |
|------|-------|-------------|
| `array` / `Array` | variadic | Create array; `[1 2 3]` desugars to this; single collection arg dispatches conversion (`(Array '(1 2 3))`, `(Array (iterator ...))`) |
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
| `type-of` / `typeof` | Type name as symbol |
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

### 7.20 FFI (Declarative)

```lisp
;; Declare a library handle
(define [ffi lib] libc "libc.so.6")

;; Bind a C function as a native Omni function
(define [ffi λ libc] (strlen (^String s)) ^Int)
(define [ffi λ libc] (abs (^Int n)) ^Int)

(strlen "hello")  ; => 5
(abs -42)          ; => 42
```

- Uses libffi via C wrapper for portable ABI support
- Type annotations: `^Int` → sint64, `^Double` → double, `^String`/`^Ptr` → pointer, `^Void` → void, `^Bool` → sint64
- Lazy dlsym: symbol resolution deferred to first call and cached

### 7.21 Constants

| Name | Value |
|------|-------|
| `true` | Symbol `true` |
| `false` | Bound to `nil` |
| `pi` | 3.141592653589793 |
| `e` | 2.718281828459045 |

### 7.22 Memory

| Primitive | Args | Description |
|-----------|------|-------------|
| `unsafe-free!` | 1 | Free heap backing of array/dict/instance/string. Value becomes an error — accessing it after free raises "use after unsafe-free!". No-op on int/nil/other non-heap types. |

**Total: 130+ primitives**

---

## 8. Standard Library

Higher-order functions and utilities defined in Omni:

| Function | Signature | Description |
|----------|-----------|-------------|
| `map` | `(f coll)` | Apply f to each element (dispatched by collection type) |
| `filter` | `(pred coll)` | Keep elements matching predicate (dispatched; lazy for iterators) |
| `foldl` | `(f acc coll)` | Left fold (dispatched) |
| `foldr` | `(f init coll)` | Right fold (finite collections) |
| `append` | `(a b)` | Concatenate lists |
| `reverse` | `(coll)` | Reverse finite collection (list or array) |
| `compose` | `(f g)` | Function composition |
| `id` | `(x)` | Identity function |
| `nth` | `(n lst)` | Nth element |
| `take` | `(n coll)` | First N elements (dispatched; lazy for iterators) |
| `drop` | `(n coll)` | Drop first N elements (dispatched; lazy for iterators) |
| `zip` | `(a b)` | Zip two collections (dispatched; lazy for iterators) |
| `range` | `(n)` | List from 0 to n-1 |
| `range-from` | `(n)` | Infinite iterator from n |
| `repeat` | `(x)` | Infinite iterator repeating x |
| `cycle` | `(coll)` | Infinite iterator cycling coll |
| `for-each` | `(f lst)` | Apply f for side effects |
| `any?` | `(pred lst)` | Any element matches? |
| `every?` | `(pred lst)` | All elements match? |
| `flatten` | `(lst)` | Flatten nested list (1 level) |
| `partition` | `(pred lst)` | Split by predicate |
| `remove` | `(pred lst)` | Remove matching elements |
| `find` | `(pred lst)` | First matching element |
| `assoc` | `(key alist)` | Association list lookup |
| `assoc-ref` | `(key alist)` | Lookup value only |

Stdlib functions take multiple parameters with strict arity. For partial application: binary primitives auto-partial `(map (+ 1) '(1 2 3))`, `_` placeholder creates lambdas `(map (+ 1 _) '(1 2 3))`, or use `partial` from stdlib.

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

- Continuations are **multi-shot**: each invocation of `k` clones the captured stack, so `k` can be called multiple times
- `k` is a function: `(k value)` resumes with `value`
- The result of `shift`'s body becomes the result of `reset`

---

## 10. Effect Handlers

### 10.1 `signal` -- Signal Effect

```lisp
(signal effect-tag argument)
```

### 10.2 `handle` -- Install Handler

```lisp
(handle body
  (effect-tag arg handler-body...)
  ...)
```

When an effect is signalled:
- `arg` is bound to the effect argument
- `handler-body` can resolve with `(resolve value)` to resume, or return a value to abort

```lisp
(handle
  (+ 1 (signal read nil))
  (read x (resolve 41)))
; => 42
```

### 10.3 `resolve` -- Resume Computation

Inside a handler clause, `(resolve value)` sends `value` back to the body.
The body continues as if `signal` returned that value.

If `resolve` is not called, the handler's return value becomes the result
of the entire `handle` expression (abort).

```lisp
; Resolve — body continues
(handle (signal double 5)
  (double x (resolve (* x 2))))
; => 10

; Abort — body abandoned
(handle (+ 1 (signal bail 42))
  (bail x x))
; => 42
```

### 10.4 I/O Effects

I/O operations go through effects with a fast path:

```lisp
; These use io/print, io/println, etc. effect tags
(println "hello")     ; fast path when no handler
(print 42)

; Custom handler intercepts I/O
(handle (begin (println "suppressed") 42)
  (io/println x (resolve nil)))
; => 42 (output suppressed)

; Capture output
(handle (begin (println "captured") nil)
  (io/println x x))
; => "captured"
```

Effect tags: `io/print`, `io/println`, `io/display`, `io/newline`, `io/read-file`, `io/write-file`, `io/file-exists?`, `io/read-lines`

### 10.5 Typed Dispatch in Handlers

Effect handlers match on tag name only. For type-specific behavior, use dispatched functions inside the handler body — this reuses the existing MethodTable dispatch system rather than introducing a parallel matching mechanism:

```lisp
(define (on-show (^Int x))    (string-append "int: " (number->string x)))
(define (on-show (^String s)) (string-append "str: " s))

(handle
  (begin (signal show 42) (signal show "hello"))
  (show x (println (on-show x)) (resolve nil)))
```

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

;; Qualified access (default)
(import math-utils)
(math-utils.add 3 4)  ; => 7

;; Selective import
(import math-utils (add multiply))
(add 3 4)  ; => 7

;; Rename on import
(import math-utils (add :as plus))
(plus 3 4)  ; => 7

;; Import all exports unqualified
(import math-utils :all)
(add 3 4)  ; => 7

;; Re-export
(export-from math-utils (add))
(export-from math-utils :all)
```

- Default import is **qualified-only**: `(import mod)` binds module as value, access via `mod.sym`
- Selective import: `(import mod (sym1 sym2))` for specific symbols
- Rename: `(import mod (sym1 :as alias))` for renaming on import
- `:all` imports all exports unqualified (opt-in)
- `export-from` re-exports symbols from another module
- File-based import: `(import "path/to/file.omni")`
- Cached: modules loaded only once
- Circular import detection
- Method extensions are always global (dispatch is cross-cutting)

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
  (let (x (signal get nil))
    (begin
      (signal put (+ x 1))
      (signal get nil)))
  (get _ (resolve 0))
  (put v (resolve nil)))
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
(Array '(1 2 3))        ; list → array conversion
(List [1 2 3])          ; array → list conversion
(Array (take 5 (range-from 0))) ; force iterator into array
(List (take 5 (range-from 0)))  ; force iterator into list

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

## 15. CLI & Project Tooling

### 15.1 Running Programs

```bash
LD_LIBRARY_PATH=/usr/local/lib ./build/main script.omni    # Run a script
LD_LIBRARY_PATH=/usr/local/lib ./build/main --repl          # Interactive REPL
```

### 15.2 Compilation

```bash
./build/main --compile input.lisp output.c3                 # Lisp → C3 source
./build/main --build input.lisp -o output                   # Lisp → standalone binary (AOT)
```

### 15.3 Project Management

```bash
./build/main --init myproject                               # Scaffold project directory
./build/main --bind myproject/                              # Generate FFI bindings from omni.toml
```

- `--init` creates `omni.toml`, `src/main.omni`, `lib/ffi/`, `include/`, `build/` (with generated `project.json`)
- `--bind` reads `omni.toml`, parses C headers via libclang, writes typed FFI modules to `lib/ffi/`
- libclang is an optional runtime dependency (only needed for `--bind`)

See `docs/PROJECT_TOOLING.md` for the complete reference including `omni.toml` format, build configuration, type mapping, and workflow examples.

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
| Symbol/string length | Dynamic (heap-allocated) |
| Total symbols | 8192 |
| Bindings per env frame | 512 |
| Match clauses | Dynamic (no fixed limit) |
| Pattern elements | Dynamic (no fixed limit) |
| Effect handler clauses | Dynamic (no fixed limit) |
| Handler stack depth | 16 |
| Call arguments | Dynamic AST (JIT compiles up to 16 natively) |
| Path segments | 8 |
| Begin expressions | Dynamic (no fixed limit) |
| Lambda params | Dynamic (no fixed limit) |
| String literal (inline) | 63 bytes (lexer limit) |
| Macros | 64 |
| Macro clauses | 8 |
| Modules | 32 |
| Module exports | 128 |
| Eval depth | 5000 |
| Registered types | 256 |
| Type fields | 16 |
| Method table entries | 64 |

---

## Appendix C: Backends

| Feature | Interpreter | JIT | Compiler |
|---------|:-----------:|:---:|:--------:|
| lambda/define/let/if | Y | Y | Y |
| begin/set!/and/or | Y | Y | Y |
| quote/quasiquote | Y | Y | Y |
| match | Y | Y | Y |
| reset/shift | Y | Y | Y |
| handle/signal/resolve | Y | Y | Y |
| type definitions | Y | Y | eval* |
| dispatch | Y | Y | eval* |
| macros | Y | Y** | Y** |
| modules | Y | Y | Y |

*eval* = delegates to interpreter for dispatch resolution
**Y** = macro expansion at parse time

---

*Omni Lisp -- A Lisp with delimited continuations, algebraic effects, strict-arity lambdas, multiple dispatch, and structural types*
