# Omnilisp Exhaustive Syntax Guide

This document provides a comprehensive set of examples for all Omnilisp features.

See also:
- `DESIGN.md` for the language specification.
- `SUMMARY.md` for the high-level overview.
- `DESIGN_DECISIONS.md` for the decision log.

---

## Implemented Syntax (C Compiler)

The current C compiler implements a **small core** of the language. Everything
below this section is the full design spec and may not be wired yet.

### Literals
```lisp
42               ; Integer
foo              ; Symbol
()               ; Empty list
```

### Unicode Identifiers
```lisp
;; Greek letters and other Unicode characters work in identifiers
(define π 3.14159)
(define α 0.5)
(define (Δ a b) (- a b))

;; Greek lambda is an alias for lambda
(λ (x) (* x x))
```

### Lists, Quote, and Function Calls
```lisp
(+ 1 2)           ; Function application (prefix)
(cons 1 (cons 2 ()))  ; List construction
'x               ; Quote -> (quote x)
; Quote preserves bracketed literal structure
'[1 2 3]         ; Quote array literal
'#{:a 1}         ; Quote dict literal
```

### Special Forms
```lisp
(define answer 42)
(define (square x) (* x x))

(lambda (x) (* x x))
(fn (x) (* x x))        ; Alias for lambda
(λ (x) (* x x))         ; Greek lambda also works

(let ((x 1) (y 2)) (+ x y))   ; List-style bindings
(let [x 1 y 2] (+ x y))       ; Array-style bindings

(if (< 1 2) 10 20)

(do
  (print "hi")
  42)                       ; Returns last expression

(begin 1 2 3)               ; Alias for do
```

### Truthiness
```lisp
; current C compiler/runtime: only false and nothing are falsy (0 and empty list are truthy)
(if 0 1 2)   ; -> 2
(if () 1 2)  ; -> 2
```

### Built-ins Wired Today
```lisp
; Arithmetic: + - * / %
; Comparison: < > <= >= =
; Lists: cons car cdr empty? list length
; Strings: string? char? string-length string-append string-ref substring string->list list->string
; Math: float? int? number? sin cos tan asin acos atan atan2 exp log log10 sqrt pow abs floor ceil round truncate ->int ->float pi e inf nan nan? inf?
; Arrays: array? array-ref array-set! array-length array-slice
; Dicts: dict? dict-ref dict-set! dict-contains? keys values
; File I/O: open close read-line read-all write-string write-line flush port? eof? file-exists?
; Higher-order: map filter reduce range
; I/O: print println
; Introspection: type-of describe keyword?
```

### Arrays & Dicts (Implemented)
```lisp
(array 1 2 3)              ; Create array
[1 2 3]                    ; Array literal
(array? arr)               ; Type predicate
(array-ref arr 0)          ; Get element at index
(array-set! arr 0 val)     ; Set element at index
(array-length arr)         ; Get length
(array-slice arr 1 4)      ; Slice [1:4]

(dict :a 1 :b 2)           ; Create dict with keyword keys
(dict? d)                  ; Type predicate
(dict-ref d :a)            ; Get value for key
(dict-set! d :a val)       ; Set value for key
(dict-contains? d :a)      ; Check if key exists
(keys d)                   ; Get all keys as list
(values d)                 ; Get all values as list
```

### Dot Access Syntax (Implemented)
```lisp
;; Field access: obj.field -> (get obj :field)
person.name                ; -> (get person :name)
person.age                 ; -> (get person :age)

;; Chained access: a.b.c -> (get (get a :b) :c)
company.ceo.name           ; -> (get (get company :ceo) :name)

;; Computed key: obj.(expr) -> (get obj expr)
arr.(0)                    ; array index
arr.(idx)                  ; variable as index
data.("string-key")        ; string key
obj.(:key)                 ; explicit keyword (same as obj.key)

;; Slicing: arr.[start end] -> (array-slice arr start end)
arr.[1 4]                  ; -> (array-slice arr 1 4)

;; Mixed access
data.items.(0)             ; nested dict then array access

;; Works on arrays, dicts, and strings
"hello".(0)                ; -> #\h (first character)
```

### Symbols & Quote Sugar (Implemented)
```lisp
;; :foo is sugar for 'foo (quoted symbol)
:foo                       ; -> foo (the symbol)
'foo                       ; -> foo (the symbol)
(= :foo 'foo)              ; -> true

;; Symbols are self-evaluating when quoted
(symbol? :foo)             ; -> true
(symbol? 'bar)             ; -> true
(symbol? 42)               ; -> false

;; .field is a getter function: (lambda (x) (get x 'field))
(map .name people)         ; extract names from list of dicts
(map .age people)          ; extract ages
(filter .active? items)    ; filter by field
```

### File I/O (Implemented)
```lisp
(open "file.txt" :read)    ; Open for reading
(open "file.txt" :write)   ; Open for writing
(open "file.txt" :append)  ; Open for appending
(read-line port)           ; Read line (returns nothing at EOF)
(read-all port)            ; Read entire file as string
(write-string port "data") ; Write string
(write-line port "data")   ; Write line with newline
(flush port)               ; Flush output buffer
(close port)               ; Close port
(port? x)                  ; Type predicate
(eof? port)                ; Check for EOF
(file-exists? "file.txt")  ; Check if file exists
```

### Destructuring in Let (Implemented)
```lisp
(let [[a b c] (list 1 2 3)] ...)     ; Array/list destructuring
(let [(cons h t) my-list] ...)       ; Cons destructuring
(let [[x [y z]] nested] ...)         ; Nested destructuring
```

### Hygienic Macros (Implemented)
```lisp
;; Define syntax transformer with pattern matching
(define [syntax my-if]
  [(my-if test then else)
   (match test [true then] [_ else])])

;; With literals declaration (symbols that match literally)
(define [syntax my-cond]
  (literals else)
  [(my-cond) nothing]
  [(my-cond [else e]) e]
  [(my-cond [test e] rest ...)
   (my-if test e (my-cond rest ...))])

;; Ellipsis patterns for zero-or-more matching
(define [syntax my-and]
  [(my-and) true]
  [(my-and x) x]
  [(my-and x y ...) (if x (my-and y ...) false)])

;; Pattern variables are bare identifiers
;; Keywords (:foo) match literally in patterns
;; ... matches zero or more elements, binds to list
```

### Modules (Implemented)
```lisp
;; Define a module with exports
(module math
  (export square cube)

  (define (square x) (* x x))
  (define (cube x) (* x x x))
  (define (private-fn x) x))  ; not exported - private

;; Import all exported symbols
(import math)
(square 4)  ; -> 16

;; Import specific symbols only
(import math :only [square])

;; Import with namespace alias
(import math :as m)
```

### Strings & Characters (Implemented)
```lisp
"hello world"          ; String literal
#\a                    ; Character literal
#\newline              ; Named character

(string-length "abc")         ; -> 3
(string-append "a" "b" "c")   ; -> "abc"
(string-ref "hello" 1)        ; -> #\e
(substring "hello" 0 3)       ; -> "hel"
(string->list "abc")          ; -> (#\a #\b #\c)
(list->string (list #\a #\b)) ; -> "ab"
(string? "x")                 ; -> true
(char? #\a)                   ; -> true
(empty? "")                   ; -> true
```

### Floats & Math (Implemented)
```lisp
3.14159                ; Float literal
1.5e-10                ; Scientific notation
0xFF                   ; Hex literal -> 255
0b1010                 ; Binary literal -> 10

(+ 1.5 2.5)            ; -> 4.0 (mixed arithmetic)
(* 2 3.14)             ; -> 6.28

; Trigonometry
(sin (/ (pi) 2))       ; -> 1.0
(cos 0)                ; -> 1.0
(atan2 1 1)            ; -> 0.785... (pi/4)

; Exponentials & Logarithms
(sqrt 2)               ; -> 1.414...
(pow 2 10)             ; -> 1024.0
(exp 1)                ; -> 2.718... (e)
(log (e))              ; -> 1.0

; Rounding
(floor 3.7)            ; -> 3.0
(ceil 3.2)             ; -> 4.0
(round 3.5)            ; -> 4.0
(truncate -3.7)        ; -> -3.0

; Conversion
(->int 3.7)            ; -> 3
(->float 42)           ; -> 42.0

; Constants & Predicates
(pi)                   ; -> 3.14159...
(e)                    ; -> 2.71828...
(float? 3.14)          ; -> true
(number? 42)           ; -> true (int or float)
```

### Pattern Matching (Implemented)
```lisp
(match x
  [1 "one"]
  [(cons h t) h]
  [(or 1 2 3) "small"]
  [_ "default"])
```

### Fibers & Channels (Implemented)
```lisp
(fiber (lambda () ...))   ; Create fiber
(spawn f)                 ; Queue fiber
(resume f)                ; Resume fiber
(yield)                   ; Yield control
(chan n)                  ; Create channel
(send ch val)             ; Send to channel
(recv ch)                 ; Receive from channel
(with-fibers body...)     ; Scoped fiber execution
```

### Comments
```lisp
; This is a comment to end-of-line
```

## Planned Syntax (Design Target)

All sections below describe the intended Omnilisp language design and are not yet implemented in the C compiler unless explicitly noted.

## Bracket Semantics

| Bracket | Context | Meaning | Example |
|---------|---------|---------|---------|
| `()` | General | Form / function call | `(+ 1 2)` |
| `()` | In pattern | Constructor pattern | `(Some v)` |
| `[]` | Expression | Array literal | `[1 2 3]` |
| `[]` | In `let` | Binding pairs/triplets | `[x 1]`, `[x {Int} 1]` |
| `[]` | In `match`/`cond` | Branch `[pattern result]` | `[[] "empty"]` |
| `[]` | In pattern | Array pattern | `[a b]`, `[h .. t]` |
| `[]` | In params | Param spec | `[x {Int}]`, `[x 10]`, `[x {Int} 10]` |
| `[]` | In struct | Field definition | `[name {Type}]` |
| `{}` | In `define` | Type-level form | `{struct ...}` |
| `{}` | Type annotation | Type wrapper | `{Int}`, `{Array Int}` |
| `#{}` | Expression | Dict literal | `#{:a 1}` |

---

## 1. Literals & Values

### 1.1 Scalars
```lisp
;; Numbers
42              ; Int
3.14            ; Float
1_000_000       ; With underscores
0xDEADBEEF      ; Hex
0b101010        ; Binary

;; Strings & Characters
"Standard string"
"String with $interpolation and \n escapes"
#\a             ; Character literal
#\newline       ; Special character

;; Symbols & Booleans
'a-symbol       ; Quoted symbol
:shorthand      ; Reader sugar for 'shorthand
true
false
nothing         ; Unit value (void/absence)
```

### 1.2 Collections
```lisp
;; Arrays (Mutable)
[1 2 3 4]
[:a "b" 10.5]
[[1 2] [3 4]]   ; Nested
[]              ; Empty array (NOT nothing)

;; Lists (Persistent)
(list 1 2 3)
(cons 1 (list 2 3))
(list)          ; Empty list (NOT nothing)

;; Tuples (Immutable fixed-width)
(tuple 1 2 "three")
(tuple :ok (tuple 10 20))

;; Named Tuples (Immutable records)
(named-tuple [x 1] [y 2])

;; Dictionaries (Key-Value)
#{:name "Alice" :age 30}
#{}             ; Empty dict (NOT nothing)
```

### 1.3 Nothing vs Empty
```lisp
;; nothing is the unit/void value
(print "hello")     ; -> nothing
(set! x 10)         ; -> nothing

;; Empty collections are distinct values
(nothing? nothing)  ; -> true
(nothing? [])       ; -> false
(nothing? (list))   ; -> false

(empty? [])         ; -> true
(empty? (list))     ; -> true
```

### 1.4 Parametric Types
```lisp
{Array Int}
{Dict Symbol String}
{Option Int}
{Result String Error}
{Array Float 2}     ; 2D Array
```

---

## 2. Bindings

### 2.1 Define (Top-Level)
```lisp
(define x 10)
(define msg "Hello")
(define items [1 2 3])

;; With metadata
(define ^:private ^:hot version 1.0)
```

### 2.2 Let (Local Bindings)
Bindings are in `[]` as an even number of forms (name-value pairs):

```lisp
;; Basic let (parallel binding)
(let [x 1
      y 2]
  (+ x y))  ; -> 3

;; Sequential let (each sees previous)
(let ^:seq [x 1
            y (+ x 1)
            z (+ y 1)]
  z)

;; Recursive let (for mutual recursion)
(let ^:rec [even? (lambda (n) (if (= n 0) true (odd? (- n 1))))
            odd?  (lambda (n) (if (= n 0) false (even? (- n 1))))]
  (even? 10))

;; Named let (loop form)
(let loop [i 0
           acc 0]
  (if (> i 10)
      acc
      (loop (+ i 1) (+ acc i))))  ; -> 55
```

### 2.3 Destructuring
```lisp
;; In define
(define [a b c] [1 2 3])
(define [first .. rest] [1 2 3 4])

;; In let
(let [[x y] [10 20]]
  (+ x y))  ; -> 30

(let ^:seq [[a .. bs] [1 2 3 4]
            sum (reduce + 0 bs)]
  sum)  ; -> 9
```

---

## 3. Functions

### 3.1 Basic Functions
```lisp
(define (add x y)
  (+ x y))

;; With type annotations (types wrapped in {})
(define (add [x {Int}] [y {Int}]) {Int}
  (+ x y))
```

### 3.1.1 Lambda Shorthand (Planned)
```lisp
(-> x (+ x 1))         ; Single-arg lambda
(-> (x y) (* x y))     ; Multi-arg lambda
```

### 3.2 Multi-Arity (via Overloads)
```lisp
;; Multi-arity handled via multiple dispatch
(define (greet)
  (greet "Guest"))

(define (greet name)
  (print "Hello, $name"))

(define (greet name title)
  (print "Hello, $title $name"))
```

### 3.3 Default Parameters ✓ IMPLEMENTED
```lisp
(define (greet [name "Guest"])
  (string-append "Hello, " name "!"))

(greet)          ; -> "Hello, Guest!"
(greet "Alice")  ; -> "Hello, Alice!"

;; Multiple defaults
(define (make-point [x 0] [y 0]) (list x y))

;; Mixed required and optional
(define (greet-with-title title [name "Guest"])
  (string-append title " " name))

;; Note: Type annotations in defaults not yet implemented
; (define (connect [host {String} "localhost"] [port {Int} 8080]) ...)
```

### 3.4 Variadic Functions ✓ IMPLEMENTED
```lisp
(define (sum .. nums)
  (reduce + 0 nums))

(sum 1 2 3 4 5)  ; -> 15
(sum)            ; -> 0

(define (log level .. args)
  (println "[" level "] " args))
```

### 3.5 Named Arguments
```lisp
;; Definition with named params after &
(define (draw shape & [color :black] [stroke {Int} 1])
  ...)

;; Call with & separator
(draw circle & :color :red :stroke 2)

;; Also accepts bracket pairs
(draw circle & [:color :red] [:stroke 2])

;; Using defaults
(draw circle)  ; color=:black, stroke=1
```

### 3.6 Partial Application
```lisp
(define plus-5 (partial add 5))
(plus-5 10)  ; -> 15

(map (partial * 2) [1 2 3])  ; -> [2 4 6]
```

---

## 4. Types

### 4.1 Abstract Types
```lisp
(define {abstract Shape} Any)
(define {abstract Polygon} Shape)
(define {abstract Animal} Any)
```

### 4.2 Structs (Immutable)
```lisp
(define {struct Point}
  [x {Float}]
  [y {Float}])

(define p (Point 10.0 20.0))
(define p2 (Point 10.0 & :y 20.0))
(define p3 (Point 10.0 & :x 10.0 :y 20.0))
p.x  ; -> 10.0
```

Positional arguments must come before `&`. Named fields follow `&`; unknown or duplicate
field names are errors.

Parent types use metadata before the header:

```lisp
(define ^:parent {Shape} {struct Circle}
  [center {Point}]
  [radius {Float}])
```

### 4.3 Mutable Structs
```lisp
(define {struct Player}
  [^:mutable hp {Int}]
  [name {String}])

;; Sugar: [hp :mutable {Int}] is equivalent to [^:mutable hp {Int}].
;; Sugar: (define ^:mutable {struct Player} ...) marks all fields mutable.
;; (define {mutable Player} ...) remains as legacy sugar.

(define hero (Player 100 "Arthur"))
(set! hero.hp 95)
```

### 4.4 Parametric Structs
```lisp
(define {struct [Pair T]}
  [first {T}]
  [second {T}])

(define {struct [Entry K V]}
  [key {K}]
  [value {V}])
```

Parent defaults to `Any`; specify `^:parent {Type}` before the `{}` header:

```lisp
(define ^:parent {Any} {struct [Entry K V]}
  [key {K}]
  [value {V}])
```

---

## 5. Sum Types (Enums) - IMPLEMENTED

### 5.1 Simple Enums
```lisp
(define {enum Color}
  Red
  Green
  Blue)

(Color? Red)   ; -> true (type predicate)
(Red? Red)     ; -> true (variant predicate)
(Green? Red)   ; -> false

(match color
  [Red "stop"]
  [Green "go"]
  [Blue "wait"])
```

Enum variants may be used unqualified when unique in scope. If ambiguous, use
`Type.Variant` (for example, `Color.Red`).

### 5.2 Enums with Data - IMPLEMENTED
```lisp
;; Define enum with data variants
(define {enum Option}
  (Some [value])    ; Data variant - creates constructor function
  None)             ; Simple variant - creates dict value

;; Use the constructor
(define x (Some 42))
(get x 'value)      ; -> 42

;; Predicates work on both simple and data variants
(Option? x)         ; -> true
(Some? x)           ; -> true
(None? x)           ; -> false
(None? None)        ; -> true

;; Result type with multiple data fields
(define {enum Result}
  (Ok [value])
  (Err [message]))

(define success (Ok 100))
(define failure (Err "not found"))

;; Type parameters (syntax parsed but not enforced at runtime)
(define {enum Option T}
  (Some [value {T}])
  None)
```

### 5.3 Using Option/Result - IMPLEMENTED
```lisp
(define (find-user id)
  (if (exists? id)
      (Some (get-user id))
      None))

;; Pattern matching extracts data from variants
(define (unwrap-or opt default)
  (match opt
    [(Some v) v]      ; Destructures Some, binds value to v
    [None default]))

(define (map-result f result)
  (match result
    [(Ok v) (Ok (f v))]
    [(Err e) (Err e)]))

;; Direct field access
(define val (get (Some 42) 'value))  ; -> 42
```

### 5.4 Recursive Enums
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

## 5.5 Algebraic Effects (OCaml 5-style)

Effects provide structured control flow with resumable handlers.

### Effect Definition
```lisp
;; Basic effect with mode
(define {effect ask} :one-shot)

;; Effect with payload type (what perform sends to handler)
(define {effect log} :one-shot
  (payload String))

;; Effect with return type (what resume sends back to perform site)
(define {effect get-config} :one-shot
  (returns String))

;; Effect with both types
(define {effect fetch} :one-shot
  (payload String)
  (returns String))
```

### Recovery Modes
- `:one-shot` - Resume at most once (default, most efficient)
- `:multi-shot` - Resume multiple times (for backtracking, non-determinism)
- `:abort` - Never resumes (like exceptions)
- `:tail` - Tail-resumptive (optimized for immediate resume)

### Using Effects
```lisp
;; Perform an effect - control jumps to handler
(perform ask "what is your name?")

;; Handle effects with resume
(handle
  (+ 1 (perform ask nothing))
  (ask (payload resume) (resume 41)))  ; -> 42

;; Abort effects for error handling
(define {effect fail} :abort (payload String))

(handle
  (do
    (println "before")
    (perform fail "oops")
    (println "after"))  ; never reached
  (fail (msg _) (string-append "Caught: " msg)))
; prints "before", returns "Caught: oops"
```

---

## 6. Multiple Dispatch

### 6.1 Generic Functions
```lisp
(define (area [s Shape])
  (error "No area for $(type-of s)"))

(define [method area Circle] (c)
  (* 3.14159 c.radius c.radius))

(define [method area Rect] (r)
  (* r.width r.height))
```

### 6.2 Multi-Argument Dispatch
```lisp
(define [method collide Circle Rect] (c r)
  (print "Circle hit Rect"))

(define [method collide Rect Circle] (r c)
  (collide c r))  ; Delegate
```

### 6.3 Interfaces (Protocols)
```lisp
(define {interface Drawable}
  (draw [self {Self}] [canvas {Canvas}])
  (bounds [self {Self}]))

(define [method draw Circle] (self canvas)
  ...)

(define [method bounds Circle] (self)
  (Rect (* 2 self.radius) (* 2 self.radius)))
```

---

## 7. Pattern Matching

`match` is a **special form**. Branches use `[]` for grouping: `[pattern result]`.

### 7.1 Basic Patterns
```lisp
(match x
  [1 "one"]
  ["hello" "greeting"]
  [:ok "symbol ok"]
  [_ "anything"])
```

### 7.2 Logic Patterns
```lisp
(match x
  [(or 1 2 3) "small"]
  [(and n (satisfies even?)) "even number"]
  [(not 0) "non-zero"])
```

`satisfies` accepts any expression that evaluates to a predicate:

```lisp
(match x
  [(satisfies (lambda (n) (> n 10))) "big"]
  [_ "small"])
```

### 7.3 Array Patterns
```lisp
(match arr
  [[] "empty"]
  [[x] "singleton"]
  [[a b] "pair"]
  [[first .. rest] "has elements"]
  [else "default"])

;; Even [[] []] is unambiguous:
(match arr
  [[] []])    ; Pattern: [], Result: []
```

### 7.4 List Patterns
```lisp
(match lst
  [(list) "empty list"]
  [(cons h t) "has head"]
  [(list a b c) "exactly three"])
```

### 7.5 Struct & Enum Patterns
```lisp
(match shape
  [(Circle r) (* 3.14 r r)]
  [(Rect w h) (* w h)])

(match result
  [(Ok value) value]
  [(Err msg) (error msg)])
```

### 7.6 Guards
Guards extend the branch to 4 elements: `[pattern :when guard result]`

```lisp
(match n
  [x :when (> x 100) "big"]
  [x :when (> x 10) "medium"]
  [_ "small"])
```

Guard expressions may be **any** expression. If the guard expression evaluates
to a function (including a lambda literal), it is **invoked as a predicate**.
The predicate receives the pattern-bound variables in left-to-right order. If
the pattern binds no variables, the predicate is invoked with the full matched
value.

```lisp
(match n
  [x :when (lambda (x) (> x 10)) "medium-or-big"]
  [_ "small"])
```

### 7.7 Alias Pattern
```lisp
(match point
  [(as (Point x y) p) :when (= x y)
   (print "diagonal: " p)])
```

---

## 8. Arrays & Sequences

### 8.1 Array Basics
```lisp
[1 2 3 4]
(array 1 2 3)

arr.(0)           ; Access
(set! arr.(0) 99) ; Mutation
(length arr)      ; Length
```

### 8.2 Functional Operations (Return New Array)
```lisp
(map f arr)       ; -> new array
(filter pred arr) ; -> new array
(reverse arr)     ; -> new array
(sort arr)        ; -> new array
(concat a b)      ; -> new array
```

### 8.3 Mutating Operations (In-Place)
```lisp
(map! f arr)      ; Mutates arr, returns nothing
(filter! pred arr)
(reverse! arr)
(sort! arr)
(push! arr elem)  ; Append element
(pop! arr)        ; Remove and return last
```

### 8.4 Aliasing & Copying
```lisp
(define a [1 2 3])
(define b a)          ; Alias (same array)
(set! a.(0) 99)
b.(0)                 ; -> 99

(define c (copy a))   ; Independent copy
(set! a.(0) 0)
c.(0)                 ; -> 99 (unchanged)
```

### 8.5 Slicing
```lisp
arr.[0 2 4]       ; Select indices -> [arr.(0) arr.(2) arr.(4)]
arr.[1:4]         ; Slice range -> new array
arr.[::2]         ; Every 2nd element
```

### 8.6 Lists
```lisp
(list 1 2 3)
(cons 1 (list 2 3))
(car lst)         ; Head
(cdr lst)         ; Tail
(empty? lst)      ; Empty check
```

### 8.7 Iteration
```lisp
(foreach [x arr]
  (print x))

(for [x (range 1 10)]
  (* x x))

(for [x items :when (even? x)]
  x)

(collect (range 1 5))     ; -> [1 2 3 4]
(iter->list (range 1 5))  ; -> (list 1 2 3 4)
(iter->array some-list)   ; -> array
```

---

## 9. Access Syntax

### 9.1 Dot Notation
```lisp
obj.field         ; Symbol field access
obj.(expr)        ; Dynamic access (computed key/index)
obj.[indices]     ; Multi-access / slice
```

### 9.2 Functional Accessors
```lisp
.name             ; -> (lambda (it) it.name)
.pos.x            ; -> (lambda (it) it.pos.x)

(map .name users)
(filter .active? items)
(sort-by .age users)
```

### 9.3 Method Calls (UFCS)
```lisp
;; Uniform Function Call Syntax
(obj.draw ctx)         ; -> (draw obj ctx)
(arr.map f)            ; -> (map arr f)
(str.split ",")        ; -> (split str ",")
```

### 9.4 Mutation
```lisp
(set! obj.field value)
(set! arr.(index) value)
```

---

## 10. Control Flow

### 10.1 Conditionals
```lisp
(if condition then-expr else-expr)

;; cond (currently a special form, planned to be a macro over match)
(cond
  [(< x 0) "negative"]
  [(> x 0) "positive"]
  [else "zero"])

;; Future: will expand to match with guards
;; (match true
;;   [_ :when (< x 0) "negative"]
;;   [_ :when (> x 0) "positive"]
;;   [_ "zero"])

(when condition
  (side-effect))

(unless condition
  (fallback))
```

### 10.2 Looping
```lisp
;; Named let recursion
(let loop [i 0]
  (when (< i 10)
    (print i)
    (loop (+ i 1))))

;; Foreach
(foreach [item items]
  (process item))

;; For comprehension
(for [x (range 1 10)
      y (range 1 10)
      :when (< x y)]
  (tuple x y))

;; Planned zip form via metadata
(for ^:zip [x xs y ys]
  (tuple x y))
```

Multiple bindings are nested by default (cartesian product). If zip mode is added,
`^:zip` metadata is preferred (`:zip` may remain as sugar).

### 10.3 Piping
```lisp
(|> value
    (f arg)           ; -> (f value arg)
    (g arg))          ; -> (g result arg)

;; With placeholder
(|> 10
    (- 100 %))        ; -> (- 100 10) -> 90

;; With accessors
(|> users
    (filter .active?)
    (map .name)
    (sort))
```

---

## 11. Concurrency

OmniLisp provides two levels of concurrency:
- **Fibers**: Lightweight cooperative coroutines (implemented)
- **Threads**: OS-level parallelism (planned)

### 11.1 Fibers (Implemented)
```lisp
;; Create and run fibers
(define f (fiber (lambda () (compute))))  ; Create paused fiber
(spawn f)                                  ; Add to scheduler queue
(resume f)                                 ; Resume and get yielded value
(join f)                                   ; Wait for completion

;; Yielding
(yield)                   ; Yield control, return nothing
(yield value)             ; Yield with value

;; Scoped execution
(with-fibers
  (spawn f1)
  (spawn f2)
  body...)                ; Runs all spawned fibers, then returns body result
```

### 11.2 Channels (Implemented)
```lisp
(define ch (chan))        ; Unbuffered (rendezvous)
(define ch (chan 10))     ; Buffered capacity 10

(send ch value)           ; Send (blocks if full/unbuffered)
(recv ch)                 ; Receive (blocks if empty)

;; Channels integrate with fiber scheduler for blocking
```

### 11.3 System Threads (Planned)
```lisp
(define t (thread (lambda () ...)))  ; Spawn OS thread
(thread-join t)                       ; Wait for completion
(thread-id)                           ; Current thread ID

;; Thread-safe primitives
(atomic-cas! ref old new)             ; Compare-and-swap
```

---

## 12. Macros

### 12.1 Hygienic Syntax Transformers (Implemented)

The macro system uses pattern-based syntax transformers:

```lisp
;; Basic macro definition
(define [syntax unless]
  [(unless condition body ...)
   (if condition nothing (do body ...))])

;; Multiple clauses with pattern matching
(define [syntax my-cond]
  (literals else)                        ; Symbols that match literally
  [(my-cond) nothing]                    ; Base case
  [(my-cond [else e]) e]                 ; Else clause
  [(my-cond [test e] rest ...)           ; Recursive case
   (if test e (my-cond rest ...))])

;; Ellipsis for zero-or-more matching
(define [syntax my-and]
  [(my-and) true]
  [(my-and x) x]
  [(my-and x y ...) (if x (my-and y ...) false)])
```

**Pattern Syntax:**
- Bare identifiers are pattern variables (bind to input)
- Keywords (`:foo`) match literally
- `(literals sym1 sym2)` declares symbols that match literally
- `x ...` matches zero-or-more, binds x to a list
- Nested patterns are supported

**Template Syntax:**
- Pattern variables are substituted with bound values
- `x ...` expands list bound to x
- `(literal expr)` protects from expansion (for macro-generating macros)

### 12.2 Quasi-Quote API (Planned)
```lisp
#'(...)           ; Syntax quote
~expr             ; Unquote
~@expr            ; Splicing unquote
(gensym)          ; Fresh symbol
(syntax->datum s) ; Extract value
(datum->syntax ctx d) ; Wrap in context
```

---

## 13. Modules

### 13.1 Definition
```lisp
(module MyModule
  (export func1 func2 MyType)

  (define (func1 ...) ...)
  (define (func2 ...) ...)
  (define {struct MyType} ...))
```

### 13.2 Import
```lisp
(import MyModule)                     ; All exports
(import [MyModule :only (func1)])     ; Specific
(import [MyModule :as M])             ; Qualified
(import [MyModule :refer (func1) :as M])
```

---

## 14. Strings

### 14.1 Interpolation
```lisp
"Hello, $name"
"Sum: $(+ x y)"
"Literal: \$100"
```

### 14.2 Prefixes
```lisp
#r"regex\d+"          ; Regex
#raw"C:\path\file"    ; Raw (no escapes)
#b"binary\x00data"    ; Byte array
```

`r"..."`, `raw"..."`, and `b"..."` are reserved in favor of `#`-prefixed forms.

### 14.3 Reader Macros (Planned)
```lisp
#? (:posix (use-posix)
    :windows (use-win)
    :else (use-generic))

#_ (expensive-debug-print x) ; discard next form
#| block comment |#

#!/usr/bin/env omnilisp

#uuid"f81d4fae-7dec-11d0-a765-00a0c91e6bf6"
#path"/usr/local/bin"
```

`#?` selects the first matching feature key; `:else` is the fallback.

---

## 15. Type System

### 15.1 Types as First-Class Objects

Types are values of type `Type`. They can be passed, returned, and stored like any other value.

```lisp
;; Types are values
Int                    ; the type Int (a value of type Type)
String                 ; the type String
(Option Int)           ; construct Option type parameterized by Int

;; Type as a value
(define t {Type} Int)  ; t holds the type Int
(equal? t Int)         ; -> true

;; Reflection
(type-of 42)           ; -> Int
(type-of "hello")      ; -> String
(equal? (type-of 42) Int)  ; -> true
```

### 15.2 Type Annotation vs Type Construction

| Syntax | Meaning | Context |
|--------|---------|---------|
| `{Type}` | Type annotation | Binding has this type |
| `(TypeCtor args)` | Type construction | Build a type from constructors |

```lisp
;; Annotation: binding has a type
[x {Int}]              ; x is annotated as Int
[path {String}]        ; path is annotated as String

;; Construction: building a type
(Option Int)           ; Option type containing Int
(Result String Error)  ; Result with Ok=String, Err=Error
(Array Float 2)        ; 2D Float array

;; Combined: annotation containing construction
[result {(Option Int)}]     ; result has type (Option Int)
[data {(Array Float 2)}]    ; data has type 2D Float array
```

### 15.3 Function Type Annotations

Return type annotates the signature directly (no `->` separator needed):

```lisp
;; Basic function with types
(define (add [x {Int}] [y {Int}]) {Int}
  (+ x y))

;; Return type is annotation after parameter list
(define (find-user [id {Int}]) {(Option User)}
  (if (exists? id)
      (Some (get-user id))
      None))

;; No annotation = inferred
(define (double x)
  (* x 2))
```

### 15.4 Type Variables (Generics)

Type variables are determined by scoping. Variables bound in the function signature are generic:

```lisp
;; A is a type variable (bound in signature)
(define (identity {A}) {A}
  [x {A}]
  x)

;; A, B are type variables
(define (map {A B}) {(List B)}
  [f {(Fn A B)}]
  [xs {(List A)}]
  (if (empty? xs)
      (list)
      (cons (f (car xs)) (map f (cdr xs)))))

;; Concrete types are not bound, looked up globally
(define (parse-int [s {String}]) {(Option Int)}
  ...)
```

### 15.5 Higher-Kinded Types

For type constructors that take other type constructors:

```lisp
;; F is a type constructor (* -> *)
(define (fmap {F A B}) {(F B)}
  [f {(Fn A B)}]
  [fa {(F A)}]
  ...)

;; Functor interface
(define {interface (Functor F)}
  (fmap [f {(Fn A B)}] [fa {(F A)}]) {(F B)})
```

### 15.6 FFI Type Declarations

FFI uses the same annotation/construction distinction:

```lisp
;; External function declaration
(define (extern torch/torch_load_tensor :nothing-on-error)
  [path {CString}]                    ; annotation on binding
  (Option (Handle Module)))           ; type construction for return

;; Opaque types with destructors
(define (opaque Tensor :destructor torch/torch_tensor_free))

;; Full example
(define (extern torch/torch_add :nothing-on-error)
  [a {(Handle Tensor)}]
  [b {(Handle Tensor)}]
  (Option (Handle Tensor)))
```

---

### 15.7 Low-Level FFI

```lisp
(@ffi "printf" "Value: %d\n" 42)
(@ffi "malloc" size)
(@ffi "free" ptr)
```

---

## 16. Metadata

```lisp
^:private            ; Keyword metadata
^:hot                ; Compiler hint
^:mutable            ; Mutability hint (fields/bindings)
^:tailrec            ; Must compile to tail call
^:borrowed           ; Non-owning parameter
^:consumes           ; Transfers ownership
^:noescape           ; Value does not escape scope
^:unchecked          ; Skip safety checks
^:deprecated         ; Deprecation marker
^"Docstring"         ; Documentation

(define ^:private ^"Internal helper"
  (helper x) ...)

(let [^:mutable counter 0]
  (set! counter (+ counter 1)))

;; Form modifiers via metadata
(let ^:seq [x 1 y (+ x 1)]
  y)

;; Tail recursion enforcement
(define ^:tailrec (sum [i {Int}] [acc {Int}])
  (if (= i 0) acc (sum (- i 1) (+ acc i))))

;; Ownership hints on parameters
(define (copy [^:borrowed src {Bytes}] [^:consumes dst {Bytes}])
  ...)

;; Unchecked access
^:unchecked arr.(i)
```

Ownership/lifetime hints are advisory in the core language and mainly enforced at
FFI/unsafe boundaries.
