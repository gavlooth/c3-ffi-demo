# Omnilisp Design Specification
Tag: Syntax Guide

See also:
- `SUMMARY.md` for the high-level overview.
- `SYNTAX.md` for the exhaustive examples.
- `DESIGN_DECISIONS.md` for the decision log.

## 1. Core Philosophy

### 1.1 Paradigm
Multi-paradigm (Functional + Imperative) with a bias toward functional idioms.

### 1.2 Type System: Julia-Style Optional Types

**OmniLisp's type system is exactly like Julia's:**
- **Types are ALWAYS optional** - you can omit them everywhere
- **Multiple dispatch on ALL arguments** - not just the first
- **No class-based OOP** - structs + multiple dispatch instead
- **Gradual typing** - add types for documentation, dispatch, or optimization

#### Type Annotation Syntax

Types are written in curly braces `{}` and are always optional:

```lisp
;; Fully untyped (like dynamic Python/JavaScript)
(define (add x y)
  (+ x y))

;; Partially typed (type just what matters)
(define (add [x {Int}] y)
  (+ x y))

;; Fully typed (explicit types everywhere)
(define (add [x {Int}] [y {Int}]) {Int}
  (+ x y))
```

#### Parameter Forms

| Syntax | Meaning |
|--------|---------|
| `x` | Untyped parameter |
| `[x {Int}]` | Typed parameter |
| `[x 10]` | Parameter with default value |
| `[x {Int} 10]` | Typed parameter with default |

#### Return Type

Place type annotation after parameters to specify return type:

```lisp
(define (square [x {Float}]) {Float}
  (* x x))
```

#### Why Julia-Style?

1. **Flexibility** - Write quick scripts without types, add them later for robustness
2. **Performance** - Compiler can optimize when types are known
3. **Dispatch** - Multiple dispatch enables clean extensibility without inheritance
4. **Interop** - Untyped code seamlessly calls typed code and vice versa

### 1.3 Syntax
Scheme-style S-expressions with extended bracket semantics:
*   `()` for forms, function calls, and constructor patterns.
*   `[]` for data-level grouping (arrays, match branches, parameter specs, struct fields).
*   `{}` for type-level forms only (types, interfaces, parametrics).
*   `#{}` for dictionary literals.

### 1.3 Bracket Semantics Summary

| Bracket | Context | Meaning | Example |
|---------|---------|---------|---------|
| `()` | General | Form / function call | `(+ 1 2)` |
| `()` | In pattern | Constructor pattern | `(Some v)`, `(Point x y)` |
| `[]` | Expression | Array literal | `[1 2 3]` |
| `[]` | In `let` | Binding pairs/triplets | `[x 1]`, `[x {Int} 1]` |
| `[]` | In `match`/`cond` | Branch `[pattern result]` | `[[] "empty"]` |
| `[]` | In pattern | Array pattern | `[a b]`, `[h .. t]` |
| `[]` | In params | Param spec | `[x {Int}]`, `[x 10]`, `[x {Int} 10]` |
| `[]` | In struct | Field definition | `[name {Type}]` |
| `{}` | In `define` | Type-level form | `{struct ...}`, `{enum ...}` |
| `{}` | Type annotation | Type wrapper | `{Int}`, `{Array Int}`, `{Self}` |
| `#{}` | Expression | Dict literal | `#{:a 1 :b 2}` |

Quote preserves bracketed literal structure:
`'(...)` is a list, `'[...]` is an array, and `'#{...}` is a dict.

### 1.3.1 Reader Macros (Planned)
Reader macros are read-time shorthands that expand before macro expansion.

*   `#(...)` - Anonymous function shorthand using `%` placeholders (`%`, `%1`, `%2`, `%&`).
*   `#?` - Reader conditional keyed by feature flags or targets.
*   `#r"..."`, `#raw"..."`, `#b"..."` - Prefixed string literals (regex, raw, bytes).
*   `#_` - Discard the next form (commenting out code).
*   `#| ... |#` - Block comment (nesting allowed).
*   `#!` - Shebang at file start (ignored by reader).
*   `#uuid"..."`, `#path"..."` - Typed literals.

```lisp
#(+ % 1)                    ; -> (lambda (x) (+ x 1))
#(* %1 %2)                  ; -> (lambda (a b) (* a b))
#? (:posix (use-posix) :windows (use-win) :else (use-generic))

#_ (expensive-debug-print x) ; discarded
#| multi-line
   comment |#

#!/usr/bin/env omnilisp

#uuid"f81d4fae-7dec-11d0-a765-00a0c91e6bf6"
#path"/usr/local/bin"
```

Reader conditionals select the first matching feature key; `:else` provides a fallback.
Typed literals expand to constructor calls (e.g., `#uuid"..."` -> `(uuid "...")`).

### 1.4 Collections
*   **Arrays:** Primary mutable sequence. `[]` literals produce mutable arrays.
*   **Lists:** Secondary persistent linked lists.
*   **Tuples:** Fixed-size immutable records for multiple returns.
*   **Dicts:** Key-value maps using `#{}` syntax (deferred to later phases).

### 1.5 Object Model
Multiple Dispatch (Julia-style). No class-based OOP.

### 1.6 Control Flow
Delimited Continuations (`prompt`/`control`), Trampolined Tail Calls.

### 1.7 Current Implemented Subset (C Compiler)
The current C compiler/runtime implements a **small core** of the language. The rest
of this document describes the intended language design.

**Implemented syntax & forms**
- Lists `(...)`
- Quote `'x` -> `(quote x)`
- Special forms: `define`, `lambda` / `fn`, `let`, `let*`, `if`, `do` / `begin`
- Function application in prefix form
- Comments start with `;` and run to end-of-line

**Binding forms**
- List-style: `(let ((x 1) (y 2)) ...)`
- Array-style: `(let [x 1 y 2] ...)` (used as a binding container)
- Default parameters: `(define (f [x default]) ...)`
- Named arguments: `(f :arg value)` - symbols starting with `:` are self-evaluating
- Destructuring: `(define [a b c] (list 1 2 3))` and `(define [x .. rest] xs)`

**Literals**
- Integers (decimal)
- Symbols
- Empty list `()`

**Primitives currently wired**
- Arithmetic: `+`, `-`, `*`, `/`, `%`
- Comparison: `<`, `>`, `<=`, `>=`, `=`
- Lists: `cons`, `car`, `cdr`, `empty?`, `list`, `length`
- I/O: `display`, `print`, `println`, `newline`
- Strings: `str`, `string-append`, `string-length`, `string-ref`, `substring`
- Higher-order: `map`, `filter`, `reduce`, `range`, `partial`, `compose`, `identity`
- Files: `open`, `close`, `read-line`, `read-all`, `write-string`, `write-line`
- Tuples: `tuple`, `tuple?`, `tuple-ref`, `named-tuple`
- Control: `try`/`catch`/`finally`, `with-open-file`, `error`

**Truthiness**
- Only `false` and `nothing` are falsy.
- **Implementation note:** the current C compiler/runtime treats only `false` and `nothing` as falsy; numeric `0` and empty lists are truthy.

---

## 1.8 Implemented Features Reference

This section provides complete documentation for features implemented in the C runtime.

### 1.8.1 Tuples

Tuples are immutable, fixed-size collections. Use them for returning multiple values or grouping related data.

```lisp
;; Create a tuple
(tuple 1 2 3)              ; -> (__tuple__ 1 2 3)
(tuple "name" 42 true)     ; Mixed types allowed

;; Type predicate
(tuple? (tuple 1 2))       ; -> true
(tuple? [1 2])             ; -> false (that's an array)

;; Access elements by index (0-based)
(define t (tuple 10 20 30))
(tuple-ref t 0)            ; -> 10
(tuple-ref t 1)            ; -> 20
(tuple-ref t 2)            ; -> 30

;; Common use: multiple return values
(define (min-max a b)
  (if (< a b)
      (tuple a b)
      (tuple b a)))

(define result (min-max 5 3))
(tuple-ref result 0)       ; -> 3 (min)
(tuple-ref result 1)       ; -> 5 (max)
```

### 1.8.2 Named Tuples

Named tuples are tuples with field names, providing readable access to elements.

```lisp
;; Create a named tuple with field names
(named-tuple [x 10] [y 20])        ; -> (__named_tuple__ (x . 10) (y . 20))
(named-tuple [name "Alice"] [age 30] [active true])

;; Access by field name
(define point (named-tuple [x 10] [y 20]))
(get point 'x)             ; -> 10
(get point 'y)             ; -> 20

;; Useful for structured data without defining a struct
(define (make-user name email)
  (named-tuple [name name] [email email] [created (current-time)]))

(define user (make-user "Bob" "bob@example.com"))
(get user 'name)           ; -> "Bob"
(get user 'email)          ; -> "bob@example.com"
```

### 1.8.3 Partial Application

`partial` creates a new function with some arguments pre-filled.

```lisp
;; Basic partial application
(define add5 (partial + 5))
(add5 10)                  ; -> 15
(add5 1)                   ; -> 6

;; With multiple pre-filled arguments
(define add10and20 (partial + 10 20))
(add10and20 5)             ; -> 35

;; Useful with higher-order functions
(map (partial * 2) [1 2 3 4])      ; -> (2 4 6 8)
(filter (partial < 5) [3 7 2 9 1]) ; -> (7 9) - elements where 5 < x

;; Creating specialized functions
(define greet (partial str "Hello, "))
(greet "World")            ; -> "Hello, World"
(greet "Alice")            ; -> "Hello, Alice"
```

### 1.8.4 Function Composition

`compose` chains functions right-to-left (mathematical composition).

```lisp
;; compose applies functions right-to-left
;; (compose f g) means: first apply g, then apply f to the result
(define inc (partial + 1))
(define double (partial * 2))

(define inc-then-double (compose double inc))
(inc-then-double 5)        ; -> 12 (5+1=6, 6*2=12)

(define double-then-inc (compose inc double))
(double-then-inc 5)        ; -> 11 (5*2=10, 10+1=11)

;; Chain multiple functions
(define process (compose str double inc))
(process 5)                ; -> "12"

;; Useful for building pipelines
(define normalize (compose string-downcase string-trim))
```

### 1.8.5 Identity Function

`identity` returns its argument unchanged. Useful as a default or placeholder.

```lisp
(identity 42)              ; -> 42
(identity "hello")         ; -> "hello"
(identity (list 1 2 3))    ; -> (1 2 3)

;; Default function argument
(define (process-list xs [transform identity])
  (map transform xs))

(process-list [1 2 3])                    ; -> (1 2 3)
(process-list [1 2 3] (partial * 2))      ; -> (2 4 6)

;; Filter nothing - keep all
(filter identity [1 nothing 2 nothing 3]) ; -> (1 2 3)
```

### 1.8.6 String Building with `str`

`str` concatenates any values into a string, converting non-strings automatically.

```lisp
;; Basic concatenation
(str "Hello" " " "World")  ; -> "Hello World"

;; Automatic conversion of non-strings
(str "Count: " 42)         ; -> "Count: 42"
(str "Pi is " 3.14159)     ; -> "Pi is 3.14159"
(str "List: " (list 1 2 3)); -> "List: (1 2 3)"

;; Building messages
(define (greet name age)
  (str "Hello, " name "! You are " age " years old."))

(greet "Alice" 30)         ; -> "Hello, Alice! You are 30 years old."

;; Convert single value to string
(str 123)                  ; -> "123"
(str true)                 ; -> "true"
```

### 1.8.7 Named Arguments

Functions with default parameters can be called with named arguments to override specific defaults.

```lisp
;; Define function with default parameters
(define (greet name [greeting "Hello"] [punctuation "!"])
  (str greeting ", " name punctuation))

;; Call with positional arguments
(greet "Alice")                          ; -> "Hello, Alice!"
(greet "Bob" "Hi")                       ; -> "Hi, Bob!"
(greet "Charlie" "Hey" "?")              ; -> "Hey, Charlie?"

;; Call with named arguments (override by name)
(greet "Alice" :greeting "Good morning") ; -> "Good morning, Alice!"
(greet "Bob" :punctuation "...")         ; -> "Hello, Bob..."
(greet "Eve" :greeting "Hi" :punctuation "~") ; -> "Hi, Eve~"

;; Named arguments can appear after positional ones
(define (make-point [x 0] [y 0] [z 0])
  (list x y z))

(make-point)               ; -> (0 0 0)
(make-point 1)             ; -> (1 0 0)
(make-point 1 2)           ; -> (1 2 0)
(make-point :z 5)          ; -> (0 0 5)
(make-point 1 :z 5)        ; -> (1 0 5)
(make-point :x 10 :y 20)   ; -> (10 20 0)
```

### 1.8.8 Top-Level Destructuring

`define` supports destructuring patterns to bind multiple variables at once.

```lisp
;; Basic list destructuring
(define [a b c] (list 1 2 3))
a                          ; -> 1
b                          ; -> 2
c                          ; -> 3

;; Destructuring function return values
(define (get-bounds items)
  (list (apply min items) (apply max items)))

(define [min-val max-val] (get-bounds [5 2 8 1 9]))
min-val                    ; -> 1
max-val                    ; -> 9

;; Rest pattern with ..
(define [first second .. rest] (list 1 2 3 4 5))
first                      ; -> 1
second                     ; -> 2
rest                       ; -> (3 4 5)

;; Head and tail
(define [head .. tail] (list 10 20 30))
head                       ; -> 10
tail                       ; -> (20 30)

;; Ignoring rest
(define [x y .. _] (list 1 2 3 4 5))
x                          ; -> 1
y                          ; -> 2
;; rest is ignored
```

### 1.8.9 Exception Handling: try/catch/finally

Simplified exception handling for common error cases. For resumable effects, use algebraic effects.

```lisp
;; Basic try/catch
(try
  (/ 10 0)
  (catch e
    (println "Error:" e)
    0))                    ; -> 0 (caught division error)

;; Catch and return default
(define (safe-parse str)
  (try
    (parse-int str)
    (catch e
      nothing)))

(safe-parse "42")          ; -> 42
(safe-parse "abc")         ; -> nothing

;; With finally (always executes)
(define (read-config path)
  (try
    (let [f (open path :read)]
      (try
        (parse-json (read-all f))
        (finally
          (close f))))     ; Always close, even on error
    (catch e
      (println "Failed to read config:" e)
      (default-config))))

;; Signaling errors
(define (divide a b)
  (if (= b 0)
      (error "Division by zero")
      (/ a b)))

(try
  (divide 10 0)
  (catch e
    (str "Caught: " e)))   ; -> "Caught: Division by zero"

;; Nested try blocks
(try
  (try
    (risky-operation)
    (catch e
      (error (str "Wrapped: " e))))  ; Re-throw wrapped
  (catch e
    (println "Outer caught:" e)))
```

### 1.8.10 Automatic Resource Management: with-open-file

`with-open-file` ensures files are closed even if an error occurs.

```lisp
;; Reading a file (auto-closes)
(with-open-file [f "data.txt" :read]
  (read-all f))            ; File closed after this returns

;; Writing to a file
(with-open-file [f "output.txt" :write]
  (write-line f "First line")
  (write-line f "Second line"))
;; File automatically closed and flushed

;; Appending to a file
(with-open-file [f "log.txt" :append]
  (write-line f (str (current-time) ": Event occurred")))

;; Processing line by line
(with-open-file [f "data.csv" :read]
  (let loop [lines []]
    (let [line (read-line f)]
      (if (nothing? line)
          lines
          (loop (cons line lines))))))

;; Modes:
;; :read   - Open for reading (file must exist)
;; :write  - Open for writing (creates or truncates)
;; :append - Open for appending (creates if needed)
```

### 1.8.11 Algebraic Effects: handle/perform/resume

OCaml 5-style algebraic effects for structured control flow with resumable handlers.

```lisp
;; Define an effect
(define {effect ask} :one-shot)

;; Perform an effect - jumps to handler
(perform ask "What is your name?")

;; Handle effects with resume
(handle
  (+ 1 (perform ask nothing))  ; Perform suspends here
  (ask (payload resume)
    (resume 41)))              ; -> 42 (1 + 41)

;; Effects can carry payloads
(define {effect log} :one-shot (payload String))

(handle
  (do
    (perform log "Starting...")
    (let [result (compute)]
      (perform log (str "Result: " result))
      result))
  (log (msg resume)
    (println "[LOG]" msg)
    (resume nothing)))         ; Continue after logging

;; Abort effects (no resume)
(define {effect fail} :abort (payload String))

(handle
  (do
    (when (< x 0)
      (perform fail "Negative value"))
    (sqrt x))
  (fail (msg _)
    (str "Error: " msg)))      ; Returns error message

;; Multi-shot effects (can resume multiple times)
(define {effect choice} :multi-shot)

(handle
  (let [x (perform choice)]
    (let [y (perform choice)]
      (list x y)))
  (choice (_ resume)
    (append (resume 1) (resume 2))))
;; -> ((1 1) (1 2) (2 1) (2 2))

;; Effect modes:
;; :one-shot   - Resume at most once (default, efficient)
;; :multi-shot - Resume multiple times (backtracking)
;; :abort      - Never resumes (like exceptions)
;; :tail       - Tail-resumptive (optimized)
```

---

## Planned Design (Not Yet Implemented in C Compiler)

The sections below describe the intended Omnilisp language design and are not yet implemented in the C compiler unless explicitly noted.

## 2. Values & Nothing

### 2.1 The `nothing` Value
`nothing` is Omnilisp's **unit value** representing absence, void, or "no meaningful result." It is the return value of side-effecting operations.

```lisp
(print "hello")  ; -> nothing
(set! x 10)      ; -> nothing
```

### 2.2 Empty Collections (Not `nothing`)
Empty collections are **distinct values**, not `nothing`:

```lisp
[]               ; Empty array (not nothing)
(list)           ; Empty list (not nothing)
(tuple)          ; Empty tuple (not nothing)
#{}              ; Empty dict (not nothing)

;; Explicit checks
(empty? [])      ; -> true
(nothing? [])    ; -> false
(nothing? nothing) ; -> true
```

Omnilisp has no `nil`. The empty list literal is `()`.

### 2.3 Optional Values
For optional/nullable semantics, use explicit wrapper types (see Section 6 on Sum Types):

```lisp
(define {enum Option T}
  (Some [value T])
  None)

;; Usage
(define (find-user id)
  (if (exists? id)
      (Some (get-user id))
      None))
```

---

## 3. Bindings & Definitions

### 3.1 Universal `define`
Omnilisp unifies all top-level bindings under `define`:

```lisp
;; Values
(define x 10)
(define msg "Hello")

;; Functions
(define (add x y) (+ x y))

;; With type annotations
(define (add [x {Int}] [y {Int}]) {Int}
  (+ x y))
```

### 3.2 Local Bindings (`let`)
Omnilisp provides a unified `let` form with Clojure-style binding syntax. Bindings are in `[]` as an even number of forms (name-value pairs).
Modifiers are expressed as metadata (`^:seq`, `^:rec`).

#### Basic Let (Parallel Binding)
All bindings are evaluated in the enclosing environment, then bound simultaneously. Supports triplets for type annotations:

```lisp
(let [x {Int} 1
      y {Int} 2]
  (+ x y))  ; -> 3
```

#### Sequential Let (`let*` behavior)
Use `^:seq` to enable sequential binding where each binding sees previous ones:

```lisp
(let ^:seq [x 1
            y (+ x 1)
            z (+ y 1)]
  z)  ; -> 3
```

#### Recursive Let (`letrec` behavior)
```lisp
(let ^:rec [even? (lambda (n) (if (= n 0) true (odd? (- n 1))))
            odd?  (lambda (n) (if (= n 0) false (even? (- n 1))))]
  (even? 10))
```

#### Named Let (Loop Form)
A `let` with a name before the bindings creates a recursive loop. Can be combined with modifiers:

```lisp
(let ^:seq loop [i {Int} 0
                 acc {Int} 0]
  (if (> i 10)
      acc
      (loop (+ i 1) (+ acc i))))  ; -> 55
```

### 3.3 Destructuring in Bindings
Both `define` and `let` support pattern destructuring:

```lisp
(define [a b c] [1 2 3])
(define [x .. rest] (some-function))

(let [[first .. rest] [1 2 3 4]]
  first)  ; -> 1

(let ^:seq [[a .. bs] [1 2 3 4]
            sum (reduce + 0 bs)]
  sum)  ; -> 9
```

### 3.4 Multi-Arity Functions
Functions with multiple arities are defined by overloading the function name. Arity dispatch is handled by the multiple dispatch system.

```lisp
(define (greet)
  (greet "Guest"))

(define (greet name)
  (print "Hello, $name"))

(define (greet name title)
  (print "Hello, $title $name"))
```

### 3.5 Default Parameters
Use `[name default]` or `[name {Type} default]` for defaults:

```lisp
(define (greet [name "Guest"])
  (print "Hello, $name"))

(define (connect [host {String} "localhost"] [port {Int} 8080])
  ...)
```

### 3.6 Variadic Functions

```lisp
(define (sum .. nums)
  (reduce + 0 nums))

(define (format template .. args)
  ...)
```

### 3.7 Named Arguments
The symbol `&` separates positional from named parameters:

```lisp
;; Definition
(define (draw shape & [color :black] [stroke {Int} 1])
  ...)

;; Call-site: & starts named argument section
(draw circle & :color :red :stroke 2)

;; Expands to:
(draw circle (named-tuple [color :red] [stroke 2]))
```

Semantics:
*   Unknown named keys are an error unless `& opts` captures the rest.
*   Duplicate named keys are an error.
*   Named arguments do not affect arity or dispatch.

### 3.8 Lambda Shorthand
`->` is reserved for lambdas (pipeline uses `|>`):

```lisp
(-> x (+ x 1))         ; (lambda (x) (+ x 1))
(-> (x y) (* x y))     ; (lambda (x y) (* x y))
```

Anonymous shorthand is also available via the reader macro `#(...)`:

```lisp
#(+ % 1)               ; (lambda (x) (+ x 1))
#(* %1 %2)             ; (lambda (a b) (* a b))
#(list %1 %2 %&)       ; (lambda (a b . rest) (list a b rest))
```

---

## 4. Control Structures

### 4.1 The Core Primitive: `match` (Special Form)

`match` is a **special form** (not a macro). The evaluator handles it directly.

Branches use `[]` for grouping - consistent with `[]` being "data-level grouping":

```lisp
(match value
  [pattern1 result1]
  [pattern2 result2]
  [else default])
```

Each branch is a `[pattern result]` pair. The special form knows:
- Element 0 = pattern
- Element 1 = result (or `:when` guard result)
- `else` is a valid wildcard pattern.
**No ambiguity:** Even `[[] []]` is clear - it's a branch where pattern is `[]` and result is `[]`.

```lisp
(match arr
  [[] "empty"]           ; Pattern: [], Result: "empty"
  [[a] a]                ; Pattern: [a], Result: a
  [[a b] (+ a b)]        ; Pattern: [a b], Result: (+ a b)
  [[] []]                ; Pattern: [], Result: [] - perfectly clear!
  [_ "default"])
```

### 4.2 Guards

Guards extend the branch to 4 elements: `[pattern :when guard result]`

```lisp
(match n
  [x :when (> x 100) "big"]
  [x :when (> x 10) "medium"]
  [_ "small"])
```

Guard expressions may be **any** expression. If the guard expression evaluates
to a function (including a lambda literal), OmniLisp **invokes it automatically**
as a predicate. The predicate receives the pattern-bound variables in
left-to-right order. If the pattern binds no variables, the predicate is invoked
with the full matched value.

```lisp
(match n
  [x :when (lambda (x) (> x 10)) "medium-or-big"]
  [_ "small"])

(match value
  [_ :when (lambda (v) (valid? v)) "ok"]
  [_ "bad"])
```

Branch structure:
- 2 elements: `[pattern result]`
- 4 elements: `[pattern :when guard-expr result]`

### 4.3 Standard Conditionals

#### `if`
```lisp
(if condition then-expr else-expr)

;; Expands to (Lisp-style truthiness: false/nothing are falsy, all else truthy):
(match condition
  [(or false nothing) else-expr]
  [_ then-expr])
```

#### `cond` (Macro)
`cond` is a macro that expands to `match`:

```lisp
(cond
  [condition1 result1]
  [condition2 result2]
  [else default])

;; Expands to:
(match true
  [_ :when condition1 result1]
  [_ :when condition2 result2]
  [_ default])
```

The `else` clause becomes a wildcard `_` without a guard.

#### `when` / `unless`
```lisp
(when condition body ...)
;; -> (if condition (begin body ...) nothing)

(unless condition body ...)
;; -> (if condition nothing (begin body ...))
```

### 4.4 Pattern Matching

#### Pattern Primitives
*   **Wildcard:** `_` matches any value, no binding.
*   **Variable:** `name` matches any value, binds it.
*   **Literal:** `1`, `"string"`, `:symbol` matches exact value.

#### Logic Patterns
```lisp
(or p1 p2 ...)   ; Match if any pattern matches
(and p1 p2 ...)  ; Match if all patterns match
(not p)          ; Match if pattern doesn't match
```

#### Constructor Patterns
```lisp
[]                       ; Empty array
[p1 p2 ..]               ; Array with elements
[h .. t]                 ; Array head/tail (rest pattern)

(list)                   ; Empty list
(list p1 p2 ..)          ; List with elements
(cons head tail)         ; Cons pattern

(tuple p1 p2 ..)         ; Tuple pattern
(named-tuple [k1 p1] ..) ; Named tuple pattern

(TypeName p1 p2 ..)      ; Struct/enum constructor pattern
```

#### Predicates
```lisp
(satisfies pred-expr)         ; pred-expr evaluated per branch
(satisfies (lambda (n) (> n 10)))
```

#### Rest Patterns
```lisp
[first .. rest]          ; Bind head and tail of array
[a b .. _]               ; Match at least 2 elements, ignore rest
```

#### Alias Pattern
```lisp
(as pattern name)        ; Match pattern and bind whole value to name
```

### 4.5 Iterators & Looping

#### Protocol
```lisp
(iterate collection)       ; -> (tuple value state) or nothing
(iterate collection state) ; -> (tuple value state) or nothing
```

#### Looping Macros
```lisp
;; Side effects
(foreach [x (range 1 10)]
  (print x))

;; Comprehension (builds array)
(for [x (range 1 10)]
  (* x x))

;; With filter
(for [x (range 1 100) :when (even? x)]
  x)

;; Multiple bindings are nested (cartesian product)
(for [x xs y ys]
  (tuple x y))
```

`for` and `foreach` use nested semantics by default. If a zip mode is added, it will
use `^:zip` metadata (preferred) with `:zip` as sugar.

### 4.6 Delimited Continuations

```lisp
(prompt body ...)          ; Establish delimiter
(control k body ...)       ; Capture continuation up to prompt, bind to k
```

### 4.7 Condition System

OmniLisp provides a Common Lisp-style condition system with structured conditions,
handlers, and restarts. Unlike exceptions in most languages, conditions support
**resumable error handling** - handlers can invoke restarts to recover without
unwinding the stack.

#### 4.7.1 Condition Types

Built-in condition type hierarchy:

```
condition
├── error
│   ├── type-error        ; Type mismatch errors
│   ├── arithmetic-error
│   │   └── division-by-zero
│   ├── unbound-variable  ; Undefined symbol
│   ├── undefined-function; Unknown function
│   └── memory-error
│       ├── use-after-free
│       ├── double-free
│       └── region-mismatch
├── ffi-error             ; Foreign function errors
├── io-error              ; I/O errors
└── warning               ; Non-fatal warnings
```

#### 4.7.2 Signaling Conditions

```lisp
;; Signal a simple condition
(signal error "something went wrong")

;; Signal with condition type
(signal type-error "expected Int, got String")

;; The error form is shorthand for signaling an error condition
(error "message")  ; Equivalent to (signal error "message")
```

#### 4.7.3 Handler-Case (Catching Conditions)

`handler-case` establishes handlers that catch conditions and **unwind the stack**
to the handler's location. Similar to try/catch in other languages.

```lisp
(handler-case
  ;; Expression to evaluate
  (risky-operation)

  ;; Handler clauses: (condition-type (var) body...)
  (error (e)
    (print "Caught error:" e)
    (quote default-value))

  (type-error (e)
    (print "Type mismatch!")
    0))
```

Multiple handlers can be specified. The first matching handler wins.

#### 4.7.4 Handler-Bind (Non-Unwinding Handlers)

`handler-bind` establishes handlers that execute **without unwinding**. The handler
can invoke restarts to resume execution at the signaling site.

```lisp
(handler-bind
  ;; Bindings: ((type handler-fn) ...)
  ((error (lambda (c)
            (print "Handling error, invoking restart")
            (invoke-restart use-value 42))))

  ;; Body to execute
  (restart-case
    (if (= x 0)
        (signal error "division by zero")
        (/ y x))

    ;; Restarts available for handlers to invoke
    (use-value (v) v)))
```

#### 4.7.5 Restart-Case (Establishing Restarts)

`restart-case` establishes named recovery points that handlers can invoke:

```lisp
(restart-case
  ;; Expression that might signal
  (parse-config-file path)

  ;; Restart clauses: (name (params) body...)
  (use-default ()
    default-config)

  (retry-with (new-path)
    (parse-config-file new-path))

  (abort ()
    nothing))
```

#### 4.7.6 Invoke-Restart

Handlers use `invoke-restart` to select a recovery strategy:

```lisp
(invoke-restart restart-name)           ; No argument
(invoke-restart restart-name value)     ; With argument
```

Example with interactive recovery:

```lisp
(restart-case
  (let [x (read-number)]
    (when (< x 0)
      (signal error "negative number"))
    x)

  (use-value (v) v)
  (use-zero () 0)
  (retry () (read-number)))

;; A handler could:
(handler-bind
  ((error (lambda (c)
            (print "Got:" c)
            (invoke-restart use-zero))))
  body)
```

#### 4.7.7 Find-Restart

Check if a restart is available:

```lisp
(if (find-restart use-value)
    (invoke-restart use-value 42)
    (invoke-restart abort))
```

#### 4.7.8 Common Restart Patterns

**Use-Value**: Return a substitute value
```lisp
(restart-case
  (/ x y)
  (use-value (v) v))

;; Handler:
(invoke-restart use-value infinity)
```

**Retry**: Attempt the operation again
```lisp
(restart-case
  (connect-to-server)
  (retry () (connect-to-server)))
```

**Abort**: Give up and return nothing
```lisp
(restart-case
  (dangerous-operation)
  (abort () nothing))
```

**Continue**: Proceed despite the condition
```lisp
(restart-case
  (when (check-fails?)
    (signal warning "check failed"))
  (continue () nothing))
```

#### 4.7.9 Implementation Notes

The condition system uses:
- **Thread-local handler stacks** for handler lookup
- **Thread-local restart stacks** for restart management
- **setjmp/longjmp** for non-local control flow
- **Structured condition objects** with typed slots

The C runtime provides:
- `condition_create()`, `condition_set_slot_*()` for condition construction
- `restart_push()`, `restart_invoke()` for restart management
- `RESTART_CASE_BEGIN/END` macros for C code

#### 4.7.10 Differences from Common Lisp

| Feature | Common Lisp | OmniLisp |
|---------|-------------|----------|
| Condition classes | CLOS classes | Simpler type registry |
| `define-condition` | Macro with slots | Not yet implemented |
| Interactive debugger | Built-in | Planned (T-debug-*) |
| Restarts interactivity | Full | Programmatic only |

---

## 5. Type System

### 5.1 Abstract Types
Define nodes in the type hierarchy for dispatch:

```lisp
(define {abstract Animal} Any)
(define {abstract Mammal} Animal)
(define {abstract Bird} Animal)
```

### 5.2 Concrete Structs
Structs are **immutable by default**:

```lisp
(define {struct Point}
  [x {Float}]
  [y {Float}])

(define ^:parent {Shape} {struct Circle}
  [center {Point}]
  [radius {Float}])
```

#### Construction
Struct constructors accept positional arguments by default and named fields after `&`:

```lisp
(define p (Point 10.0 20.0))
(define p2 (Point 10.0 & :y 20.0))
(define p3 (Point 10.0 & :x 10.0 :y 20.0))
```

Rules:
*   Positional arguments must come before `&`.
*   Named fields follow `&` and use `:field value` pairs.
*   Unknown or duplicate field names are errors.

#### Mutable Structs
```lisp
(define {struct Player}
  [^:mutable hp {Int}]
  [name {String}]
  [pos {Point}])

;; Sugar: [hp :mutable {Int}] is equivalent to [^:mutable hp {Int}].
;; Sugar: (define ^:mutable {struct Player} ...) marks all fields mutable.
;; (define {mutable Player} ...) remains as legacy sugar.

(define hero (Player 100 "Arthur" (Point 0 0)))
(set! hero.hp 95)
```

### 5.3 Parametric Types
Use `{}` for type parameters:

```lisp
{Array Int}              ; Array of integers
{Dict Symbol String}     ; Dictionary
{Array Float 2}          ; 2D array
{Option Int}             ; Optional integer
```

#### Defining Parametric Types
Parent defaults to `Any`. To specify a parent, attach `^:parent {Type}` metadata
before the `{struct ...}` header:

```lisp
(define {struct [Pair T]}
  [first {T}]
  [second {T}])

(define {struct [Entry K V]}
  [key {K}]
  [value {V}])

(define ^:parent {Any} {struct [Entry K V]}
  [key {K}]
  [value {V}])
```

---

## 6. Sum Types (Enums)

Omnilisp supports algebraic data types via `enum`:

### 6.1 Basic Enums
```lisp
(define {enum Color}
  Red
  Green
  Blue)

(match color
  [Red "stop"]
  [Green "go"]
  [Blue "wait"])
```

Enum variants may be used unqualified when unique in scope. If ambiguous, use
`Type.Variant` (for example, `Color.Red`).

### 6.2 Enums with Data
```lisp
(define {enum Option T}
  (Some [value {T}])
  None)

(define {enum Result T E}
  (Ok [value {T}])
  (Err [error {E}]))

(define {enum List T}
  (Cons [head {T}] [tail {List T}])
  Empty)
```

### 6.3 Pattern Matching on Enums
```lisp
(define (unwrap-or opt default)
  (match opt
    [(Some v) v]
    [None default]))

(define (map-result f result)
  (match result
    [(Ok v) (Ok (f v))]
    [(Err e) (Err e)]))
```

### 6.4 Recursive Enums
```lisp
(define {enum Expr}
  (Lit [value {Int}])
  (Add [left {Expr}] [right {Expr}])
  (Mul [left {Expr}] [right {Expr}]))

(define (eval expr)
  (match expr
    [(Lit n) n]
    [(Add l r) (+ (eval l) (eval r))]
    [(Mul l r) (* (eval l) (eval r))]))
```

---

## 7. Multiple Dispatch

### 7.1 Generic Functions
```lisp
;; Declare generic with fallback
(define (area [s Shape])
  (error "No area method for $(type-of s)"))

;; Implement for specific types
(define [method area Circle] (c)
  (* 3.14159 c.radius c.radius))

(define [method area Rect] (r)
  (* r.width r.height))
```

### 7.2 Multi-Argument Dispatch
```lisp
(define [method collide Circle Circle] (a b)
  ...)

(define [method collide Circle Rect] (c r)
  ...)

(define [method collide Rect Circle] (r c)
  (collide c r))  ; Delegate to other method
```

### 7.3 Specificity Rules
*   Method A is more specific than B if all parameter types in A are subtypes of corresponding types in B.
*   If no unique most-specific method exists, an ambiguity error is raised.
*   Parametric types are invariant.

### 7.4 Interfaces (Protocols)
Interfaces define explicit contracts for generic functions. `{Self}` is used as a placeholder for the implementing type.

```lisp
(define {interface Drawable}
  "Protocol for renderable objects."
  (draw [self {Self}] [ctx {Context}])
  (bounds [self {Self}]))

;; Implementation via methods
(define [method draw Circle] (self ctx)
  ...)

(define [method bounds Circle] (self)
  (Rect (* 2 self.radius) (* 2 self.radius)))
```

---

## 8. Arrays & Sequences

### 8.1 Array Basics
Arrays are mutable, contiguous sequences:

```lisp
[1 2 3 4]                ; Array literal
(array 1 2 3)            ; Constructor form

;; Access
arr.(0)                  ; First element
arr.(n)                  ; nth element (0-indexed)

;; Mutation
(set! arr.(0) 99)
```

### 8.2 Mutation Semantics

**Principle:** Sequence functions return **new arrays** by default. Use `!`-suffixed variants for in-place mutation.

```lisp
;; Functional (return new array)
(map f arr)              ; -> new array
(filter pred arr)        ; -> new array
(reverse arr)            ; -> new array

;; Imperative (mutate in place, return nothing)
(map! f arr)             ; Mutates arr, returns nothing
(filter! pred arr)       ; Mutates arr, returns nothing
(reverse! arr)           ; Mutates arr, returns nothing
(sort! arr)              ; Mutates arr, returns nothing
```

### 8.3 Aliasing & Copying
Arrays are reference types. Assignment creates aliases:

```lisp
(define a [1 2 3])
(define b a)             ; b is an alias to a
(set! a.(0) 99)
b.(0)                    ; -> 99 (same array)

;; Explicit copy
(define c (copy a))      ; c is independent
(set! a.(0) 0)
c.(0)                    ; -> 99 (unchanged)
```

### 8.4 Slicing & Projection
```lisp
arr.[0 2 4]              ; Select indices -> new array [arr.(0) arr.(2) arr.(4)]
arr.[1:3]                ; Slice range -> new array

;; Slices are copies, not views
(define slice arr.[0:2])
(set! arr.(0) 999)
slice.(0)                ; Unchanged (slice is a copy)
```

### 8.5 Lists (Persistent)
```lisp
(list 1 2 3)             ; Linked list
(cons 1 (list 2 3))      ; Prepend
(car lst)                ; Head
(cdr lst)                ; Tail

;; Empty list
(list)                   ; -> empty list (NOT nothing)
(empty? (list))          ; -> true
(empty? [])              ; -> true (works for all collections)
```

### 8.6 Tuples
Fixed-size immutable sequences for multiple returns:

```lisp
(tuple 1 2 3)
(values a b c)           ; Sugar for tuple in return position

;; Destructuring
(define (tuple x y) (min-max 10 20))

;; Access
tup.(0)                  ; First element
```

### 8.7 Iteration Protocol
All sequences implement `iterate`:

```lisp
(collect (range 1 5))    ; -> [1 2 3 4]
(iter->list (range 1 5)) ; -> (list 1 2 3 4)
(iter->array some-list)  ; -> array from list
```

---

## 9. Access Syntax

### 9.1 Dot Notation
```lisp
obj.field                ; -> (get obj 'field)
obj.(expr)               ; -> (get obj expr) - dynamic access
obj.[indices]            ; -> multi-access / slice
```

### 9.2 Functional Accessors
A leading dot creates a getter function:

```lisp
.name                    ; -> (lambda (it) it.name)
.pos.x                   ; -> (lambda (it) it.pos.x)
.items.(0)               ; -> (lambda (it) it.items.(0))

;; Usage
(map .name users)        ; Extract names from users
(filter .active? items)  ; Filter by predicate field
```

### 9.3 Method Calls (UFCS)
Uniform Function Call Syntax: `(obj.method args)` expands to `(method obj args)`.

```lisp
(obj.draw ctx)             ; -> (draw obj ctx)
(arr.map f)                ; -> (map arr f)
(str.split ",")            ; -> (split str ",")
```

### 9.4 Mutation
```lisp
(set! obj.field value)
(set! obj.(index) value)
(set! (.field obj) value)  ; Equivalent
```

---

## 10. Macros

### 10.1 Hygienic Macros
```lisp
(define [macro unless] (condition body)
  #'(if (not ~condition) ~body nothing))

(define [macro with-log] (msg . body)
  #'(begin
      (print "Start: " ~msg)
      (let [result (begin ~@body)]
        (print "End: " ~msg)
        result)))
```

### 10.2 Core API
*   `syntax` / `#'` - Create syntax object
*   `syntax->datum` - Extract value from syntax
*   `datum->syntax` - Wrap value in syntax context
*   `gensym` / `fresh` - Generate unique symbol
*   `~` - Unquote in syntax template
*   `~@` - Splicing unquote

---

## 11. Piping & Composition

### 11.1 Pipe Operator
```lisp
(|> value
    (f arg1)           ; -> (f value arg1)
    (g arg2))          ; -> (g result arg2)
```

### 11.2 Placeholder
Use `%` to control placement:

```lisp
(|> 10
    (- 100 %))         ; -> (- 100 10) -> 90
```

### 11.3 With Functional Accessors
```lisp
(|> users
    (filter .active?)
    (map .name)
    (sort))
```

---

## 12. Concurrency

### 12.1 Green Threads
```lisp
(define p (spawn (lambda () (compute-something))))
```

### 12.2 Channels
```lisp
(define ch (channel))        ; Unbuffered
(define ch (channel 10))     ; Buffered (capacity 10)

(send ch value)              ; Send (blocks if full), returns false if closed
(recv ch)                    ; Receive (blocks if empty)
(close ch)                   ; Close channel

;; Receive from closed channel returns nothing
;; Send returns true on success, false if channel is closed
```

### 12.3 Park / Unpark
```lisp
(park)                       ; Suspend current thread
(unpark process value)       ; Resume process with value
```

### 12.4 Yield
```lisp
(yield)                      ; Cooperative yield to scheduler
```

---

## 13. Modules

### 13.1 Definition
```lisp
(module MyModule
  (export function1 function2 TypeName)

  (define (function1 ...) ...)
  (define (function2 ...) ...)
  (define {struct TypeName} ...))
```

### 13.2 Import
```lisp
(import MyModule)                    ; Import all exports
(import [MyModule :only (f1 f2)])    ; Import specific
(import [MyModule :as M])            ; Qualified import
(import [MyModule :refer (f1) :as M]) ; Mixed
```

---

## 14. Strings

### 14.1 Interpolation
```lisp
"Hello, $name"
"Sum: $(+ x y)"
"Escape: \$100"
```

### 14.2 String Prefixes
```lisp
#r"regex\d+"             ; Regex (Pika-powered)
#raw"C:\path\file"       ; Raw string (no escapes)
#b"binary\x00data"       ; Byte array
```

`r"..."`, `raw"..."`, and `b"..."` are reserved in favor of `#`-prefixed forms.

---

## 15. Foreign Function Interface

> **Full Documentation**: See [FFI_PROPOSAL.md](./FFI_PROPOSAL.md) for comprehensive FFI design including library loading, type system, ownership annotations, memory regions, callbacks, and complete examples (SDL2, SQLite, signal handling).

### 15.1 Quick Syntax

```lisp
;; Basic FFI call (low-level)
(@ffi "function_name" arg1 arg2 ...)
(@ffi "printf" "Value: %d\n" 42)

;; Library loading
(import {ffi "libfoo.so" :as foo})

;; Function declaration with ownership
(define {extern malloc :from libc}
  [size {CSize}]
  -> {^:owned CPtr})

;; Safe handle-based usage
(with-ffi [h (ffi/alloc {Point})]
  (set! (deref h).x 10.0)
  (process h))
;; h automatically freed
```

### 15.2 Key Features

| Feature | Description |
|---------|-------------|
| Handle-based safety | 64-bit handles with generation counters prevent use-after-free |
| Ownership annotations | `^:owned`, `^:borrowed`, `^:consumed`, `^:escapes` |
| Region allocation | Arena, linear, offset, pool regions via IRegion infrastructure |
| Safe callbacks | WeakHandles that invalidate when closures are freed |
| Deterministic mode | Sequential handle allocation for replay/debugging |

Type marshalling is automatic between Omnilisp and native types.

---

## 16. Metadata & Hints

```lisp
^metadata object
^:keyword object

;; Examples
(define ^:private ^:hot internal-fn ...)
(define ^"Docstring here" (my-func x) ...)
(let [^:mutable counter 0]
  (set! counter (+ counter 1)))

(define {struct Player}
  [^:mutable hp {Int}]
  [name {String}])

(define ^:tailrec (sum [i {Int}] [acc {Int}])
  (if (= i 0) acc (sum (- i 1) (+ acc i))))

(define (copy [^:borrowed src {Bytes}] [^:consumes dst {Bytes}])
  ...)

^:unchecked arr.(i)
```

Metadata is stored separately and does not affect identity or equality.
Fields are immutable unless marked `^:mutable` (or `:mutable` sugar). The compiler may
warn or error when mutating a binding or field that is not marked `^:mutable`.

Common hints (non-semantic):
*   `^:inline`, `^:noinline`
*   `^:pure`
*   `^:hot`, `^:cold`
*   `^:mutable`
*   `^:deprecated` (optional docstring payload)

Ownership/lifetime hints (primarily for FFI/unsafe boundaries; advisory in core language):
*   `^:borrowed` (does not transfer ownership)
*   `^:consumes` (transfers ownership to callee)
*   `^:noescape` (value does not escape scope)

Safety/perf hints:
*   `^:unchecked` (skip bounds checks or safety checks)
*   `^:tailrec` (must compile to tail call)

Form modifiers (semantic, preferred over ad-hoc keywords):
*   `^:seq` / `^:rec` on `let`
*   `^:zip` on `for` / `foreach` (if added)

Structural metadata:
*   `^:parent {Type}` before `{struct ...}` headers

---

## 17. Special Forms vs Macros

### Special Forms (handled by evaluator)
*   `match` - Pattern matching
*   `if` - Binary conditional
*   `define` - Bindings
*   `let` - Local bindings
*   `lambda` - Function creation
*   `set!` - Mutation
*   `begin` - Sequencing
*   `quote` / `'` - Quoting
*   `prompt` / `control` - Continuations

### Macros (expand to special forms)
*   `cond` - Expands to `match` with guards
*   `when` / `unless` - Expand to `if`
*   `for` / `foreach` - Expand to iterator calls
*   `|>` - Expand to nested calls
*   User-defined macros via `define [macro ...]`

### Reader Macros (handled by reader)
*   `#(...)` - Anonymous function shorthand with `%` placeholders
*   `#?` - Reader conditionals
*   `#r` / `#raw` / `#b` - Prefixed string literals
*   `#_` - Discard next form
*   `#| |#` - Block comment
*   `#!` - Shebang (file start)
*   `#uuid` / `#path` - Typed literals

---

## 18. Phase-1 Scope

### Include
*   Reader: `()`, `[]`, `{}`, `#{}`, dot access, `:sym` sugar, core literals, `#(...)`, `#?`, `#_`, `#| |#`, `#!`, `#r/#raw/#b`, `#uuid/#path`
*   Unified `define`: values, functions, methods, macros, types
*   `let` with `^:seq` and `^:rec` metadata
*   Type system: abstract types, structs, parametric types, enums
*   Multiple dispatch with specificity rules
*   `match` as special form with `[]` branches; `cond` as macro expanding to `match`
*   Pattern matching: all core patterns, guards, no user-defined patterns
*   Basic hygienic macros
*   Iterator protocol + `foreach`/`for`
*   Module system
*   Basic error handling (`error` + single restart)
*   Trampolined TCO
*   Cooperative concurrency: `spawn`, `yield`, channels
*   Mutable arrays, persistent lists, tuples

### Defer
*   Extensible patterns (`define [pattern ...]`)
*   Full condition system (multi-restart, `handler-bind`)
*   Dicts and sets (basic `#{}` syntax available, full API later)
*   Advanced FFI (callbacks, structs)

---

## 19. Tower Semantics (Meta-Level Programming)

OmniLisp supports **tower of interpreters** semantics based on the "Collapsing Towers of Interpreters" paper. This enables multi-stage programming where code can be generated, inspected, and executed at different meta-levels.

### 19.1 Core Concepts

The tower consists of multiple levels:
- **Level 0**: Base runtime (C code execution)
- **Level 1**: Interpreted evaluation (default)
- **Level N**: Higher meta-levels for code generation

Each level has its own **handlers** that define evaluation semantics.

### 19.2 Tower Forms

| Form | Description |
|------|-------------|
| `(lift expr)` | Lift value to code representation (go up one level) |
| `(run code)` | Execute code at base level (go down to level 0) |
| `(EM expr)` | Escape to Meta - evaluate at parent level |
| `(shift n expr)` | Go up n levels and evaluate |
| `(meta-level)` | Get current tower level as integer |

### 19.3 Lifting and Running

```lisp
;; Lift a value to its code representation
(lift 42)           ; -> Code representing mk_int(42)
(lift (+ 1 2))      ; -> Evaluates to 3, then lifts to mk_int(3)

;; Run code at base level
(run (lift 42))     ; -> Execute the C code, get 42 back

;; Multi-stage: generate optimized code
(define (power-gen n)
  (if (= n 0)
      (lift 1)
      `(* x ~(power-gen (- n 1)))))

(power-gen 3)       ; -> (* x (* x (* x 1)))
```

### 19.4 Handler System

Handlers define the semantics of each evaluation phase. OmniLisp uses `[]` brackets for handler specifications:

| Handler | Purpose |
|---------|---------|
| `h-lit` | Literal handling |
| `h-var` | Variable lookup |
| `h-lam` | Lambda creation |
| `h-app` | Function application |
| `h-if` | Conditional evaluation |
| `h-lft` | Lift operation |
| `h-run` | Run operation |
| `h-em` | Escape to meta |
| `h-clam` | Compile lambda |

### 19.5 Handler Manipulation

```lisp
;; Get a handler
(get-meta 'h-app)            ; -> current app handler

;; Set a handler (returns new menv)
(set-meta! 'h-app my-handler)

;; Scoped handler changes
(with-handlers [[h-lit custom-lit]
                [h-app custom-app]]
  body ...)
```

### 19.6 Custom Evaluation Semantics

Define custom handlers to modify evaluation:

```lisp
;; Tracing evaluator - log every application
(define (tracing-app expr menv)
  (let [fn (car expr)
        args (cdr expr)]
    (print "Calling: " fn " with " args)
    (default-handler 'h-app expr)))  ; Delegate to default

(with-handlers [[h-app tracing-app]]
  (+ 1 2 3))
;; Prints: Calling: + with (1 2 3)
;; Returns: 6
```

### 19.7 Code Generation with Tower

The tower enables **staged compilation**:

```lisp
;; Stage 0: Define a code generator
(define (gen-loop n body-gen)
  #'(let loop [i 0]
      (if (< i ~(lift n))
          (begin
            ~(body-gen 'i)
            (loop (+ i 1)))
          nothing)))

;; Stage 1: Generate specialized code
(gen-loop 10 (lambda (i) #'(print ~i)))
;; -> Expands to optimized loop code

;; Stage 2: Execute
(run (gen-loop 10 (lambda (i) #'(print ~i))))
```

### 19.8 Escape to Meta (EM)

`EM` evaluates an expression at the parent meta-level:

```lisp
;; Inside a meta-level computation
(EM (+ 1 2))        ; Evaluate (+ 1 2) at parent level

;; Useful for accessing outer bindings
(let [x 10]
  (with-handlers [[h-var custom-var]]
    (EM x)))        ; Access x using parent's h-var, not custom-var
```

### 19.9 Compile Lambda (clambda)

`clambda` compiles a lambda under current handler semantics:

```lisp
(clambda (x) (* x x))   ; Compile with current handlers

;; In a lifted context, produces code for a function
(with-handlers [[h-lit lift-handler]
                [h-app gen-app]]
  (clambda (x) (* x x)))
;; -> Generates C code for the function
```

### 19.10 Integration with Macros

Tower semantics work with hygienic macros:

```lisp
(define [macro staged-power] (n)
  (if (= n 0)
      #'1
      #'(* x ~(staged-power (- n 1)))))

;; At compile time, generates specialized multiplication chain
(define (power5 x)
  (staged-power 5))
;; Expands to: (* x (* x (* x (* x (* x 1)))))
```

---

## 20. Implementation Status (C Compiler)

### Implemented
- Parser: integers, symbols, lists `(...)`, arrays `[...]` (used for `let` bindings), quote `'`
- Special forms: `define`, `lambda` / `fn`, `let`, `let*`, `if`, `do` / `begin`
- Primitives: `+`, `-`, `*`, `/`, `%`, `<`, `>`, `<=`, `>=`, `=`, `cons`, `car`, `cdr`, `empty?`
- I/O: `display`, `print`, `newline`
- ASAP analysis pass integrated into codegen for compile-time `free_obj` insertion

### Not Yet Wired in the C Compiler
- Strings, floats, chars, dicts, tuples, type literals
- Pattern matching, macros, modules, multiple dispatch
- Concurrency/FFI surface syntax and tower semantics

---

## 21. System Architecture

Omnilisp provides three execution paths: interpreter, JIT, and AOT compilation.

### 21.1 Core Components

```
┌─────────────────────────────────────────────────────────────────┐
│                         Omnilisp CLI                             │
│  ./omni <expr>     - Interpret expression                       │
│  ./omni <file>     - Run file                                   │
│  ./omni --jit      - JIT compile and run                        │
│  ./omni --compile  - AOT compile to C                           │
│  ./omni --build    - AOT compile to executable                  │
└───────────────┬─────────────────┬─────────────────┬─────────────┘
                │                 │                 │
        ┌───────▼───────┐ ┌───────▼───────┐ ┌───────▼───────┐
        │   Reader      │ │   Reader      │ │   Reader      │
        │ (pika parser) │ │ (pika parser) │ │ (pika parser) │
        └───────┬───────┘ └───────┬───────┘ └───────┬───────┘
                │                 │                 │
        ┌───────▼───────┐ ┌───────▼───────┐ ┌───────▼───────┐
        │  Evaluator    │ │  Compiler     │ │  Compiler     │
        │ (tree-walk)   │ │ (emit C)      │ │ (emit C)      │
        └───────┬───────┘ └───────┬───────┘ └───────┬───────┘
                │                 │                 │
                ▼                 ▼                 ▼
           Result           gcc → .so           gcc → exe
                           dlopen/call          standalone
```

### 21.2 Tagged Pointer Representation

All runtime values use tagged pointers with a 3-bit tag in the low bits:

| Tag | Value Type | Representation |
|-----|------------|----------------|
| `0x0` | Heap pointer | Aligned pointer to `Obj` |
| `0x1` | Immediate int | `(n << 3) \| 0x1` |
| `0x2` | Character | `(c << 3) \| 0x2` |
| `0x3` | Boolean | `(b << 3) \| 0x3` |
| `0x4` | Symbol ID | `(id << 3) \| 0x4` |
| `0x5` | Reserved | — |
| `0x6` | Reserved | — |
| `0x7` | Special | empty-list, nothing, eof |

Immediate integers fit in 61 bits (signed), avoiding heap allocation for common small values.

### 21.3 Object Layout

Heap-allocated objects share a common header:

```c
typedef struct Obj {
    uint16_t generation;  // For generational refs
    int mark;             // Reference count / GC mark
    int tag;              // TAG_INT, TAG_FLOAT, TAG_SYM, etc.
    int is_pair;          // 1 if cons pair
    int scc_id;           // SCC identifier for cycle handling
    unsigned int scan_tag;
    union {
        long i;                        // Integer
        double f;                      // Float
        struct { struct Obj *a, *b; }; // Pair (car, cdr)
        void* ptr;                     // Symbol string, etc.
    };
} Obj;
```

### 21.4 JIT Compilation

JIT compilation emits self-contained C code with an embedded mini-runtime:

1. Generate C source with tagged pointer macros and primitive functions
2. Compile to shared library: `gcc -shared -fPIC -O2 -o /tmp/jit.so /tmp/jit.c`
3. Load with `dlopen()` and call the entry point via `dlsym()`
4. Return tagged pointer result

The standalone header includes:
- `MAKE_INT_IMM(n)` - Create immediate integer
- `prim_add`, `prim_sub`, `prim_mul`, `prim_div`, `prim_mod` - Arithmetic
- `prim_lt`, `prim_gt`, `prim_le`, `prim_ge`, `prim_eq` - Comparisons

### 21.5 AOT Compilation

AOT compilation produces standalone executables:

1. `--compile src.lisp out.c` - Generate C source file
2. `--build src.lisp out` - Generate C and compile to executable

AOT executables include the full standalone runtime and require no external libraries.

### 21.6 Memory Management (ASAP)

Omnilisp uses ASAP (As Static As Possible) memory management:

- **No garbage collector** - All `free()` calls inserted at compile time
- **Liveness analysis** - Free at last use, not scope exit
- **Escape analysis** - Stack-allocate non-escaping values
- **Capture tracking** - Lambda-captured variables transfer ownership
- **Automatic weak back-edges** - Compiler detects and handles cycles

See `CLAUDE.md` for detailed ASAP documentation.

### 21.7 Control Flow Primitives

#### Delimited Continuations: `prompt`/`control`

```lisp
(prompt body...)           ; Establish delimitation boundary
(control k body)           ; Capture continuation to k, evaluate body
```

Example:
```lisp
(prompt (+ 1 (control k 42)))  ; → 42 (aborts to prompt)
(prompt (control k (k 10)))    ; → 10 (invokes continuation)
```

#### Trampolining: `bounce`/`trampoline`

For mutual recursion without stack overflow:

```lisp
(bounce fn args...)        ; Create thunk (don't call fn yet)
(trampoline fn args...)    ; Call fn, loop while result is bounce
(bounce? v)                ; Check if v is a bounce thunk
```

Example - mutual recursion:
```lisp
(define (even? n)
  (if (= n 0) true (bounce odd? (- n 1))))

(define (odd? n)
  (if (= n 0) false (bounce even? (- n 1))))

(trampoline even? 100000)  ; → true (no stack overflow)
```

| Mechanism | Use Case | Stack Growth |
|-----------|----------|--------------|
| Direct recursion | Simple cases | O(n) - may overflow |
| `bounce`/`trampoline` | Mutual recursion | O(1) - heap allocation |
| `prompt`/`control` | Advanced control flow | O(1) - setjmp/longjmp |
| Compiler TCO | Self-tail-recursion | O(1) - loop conversion |

#### Compile-Time Tail Call Optimization

The compiler automatically converts self-tail-recursive calls to loops. This is a compile-time transformation with zero runtime overhead.

**What gets optimized:**
- Self-recursive calls in tail position (last expression of function body)
- Tail calls inside `if` branches, `do` blocks, and `let` bodies

**Example:**
```lisp
(define (factorial-iter n acc)
  (if (= n 0)
      acc
      (factorial-iter (- n 1) (* n acc))))  ; Tail call → goto

(factorial-iter 1000000 1)  ; No stack overflow
```

**Generated C code structure:**
```c
static Obj* fn_factorial_iter(Obj* n, Obj* acc) {
TCO_START:;
    if (n == 0) return acc;
    // TCO: reassign parameters and jump
    Obj* new_n = n - 1;
    Obj* new_acc = n * acc;
    n = new_n;
    acc = new_acc;
    goto TCO_START;  // No function call!
}
```

**Choosing the right mechanism:**

| Situation | Use |
|-----------|-----|
| Simple self-recursion | Just write it - compiler handles TCO |
| Mutual recursion (A calls B calls A) | `bounce`/`trampoline` |
| Non-tail recursion (needs CPS) | `bounce`/`trampoline` with `identity` |
| Early exit, backtracking | `prompt`/`control` |

#### CPS Trampolining with `identity`

For non-tail recursion, use CPS transformation with `identity` as the final continuation:

```lisp
;; Non-tail recursion (stack grows with depth)
(define (sum-list lst)
  (if (empty? lst) 0
      (+ (car lst) (sum-list (cdr lst)))))

;; CPS + trampoline (constant stack)
(define (sum-list-k lst k)
  (if (empty? lst)
      (k 0)
      (bounce sum-list-k (cdr lst)
              (fn [rest] (k (+ (car lst) rest))))))

(trampoline sum-list-k my-list identity)  ; identity is the initial continuation
```

The `identity` function simply returns its argument, serving as the "done" continuation.
