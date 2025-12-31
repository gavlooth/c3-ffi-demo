# Purple Language Reference

Purple is a stage-polymorphic language implementing the "Collapsing Towers of Interpreters" paradigm (Amin & Rompf, POPL 2018). It features compile-time memory management via ASAP (As Static As Possible) analysis.

## Table of Contents

1. [Data Types](#data-types)
2. [Special Forms](#special-forms)
3. [Pattern Matching](#pattern-matching)
4. [Primitives](#primitives)
5. [Staging (Tower of Interpreters)](#staging-tower-of-interpreters)
6. [Handler Customization](#handler-customization)
7. [Macro System](#macro-system)
8. [FFI (Foreign Function Interface)](#ffi-foreign-function-interface)
9. [Error Handling](#error-handling)

---

## Data Types

### Integers
```scheme
42
-17
0
```

### Floating Point
```scheme
3.14
-2.5
1.0e10
```

### Symbols
```scheme
foo
my-variable
+
```

### Characters
```scheme
#\a          ; lowercase a
#\Z          ; uppercase Z
#\newline    ; newline character
#\space      ; space character
#\tab        ; tab character
```

### Lists (Pairs)
```scheme
'(1 2 3)              ; quoted list
(cons 1 (cons 2 nil)) ; explicit construction
(list 1 2 3)          ; list primitive
```

### Nil
```scheme
nil
'()
```

### Booleans
Purple uses nil for false and any non-nil value for true. The symbol `t` is conventionally used for true.
```scheme
t      ; true
nil    ; false
```

### Code Values
Generated during staging/compilation:
```scheme
(lift 42)    ; => Code: mk_int(42)
```

### Closures
Created by `lambda`:
```scheme
(lambda (x) (+ x 1))
```

---

## Special Forms

### lambda - Create Functions
```scheme
; Simple lambda
(lambda (x) (+ x 1))

; Multiple parameters
(lambda (x y) (+ x y))

; Recursive lambda with self-reference
(lambda self (n)
  (if (= n 0)
      1
      (* n (self (- n 1)))))
```

### let - Local Bindings
```scheme
; Single binding
(let ((x 10)) (+ x 5))    ; => 15

; Multiple bindings
(let ((x 1) (y 2) (z 3))
  (+ x (+ y z)))          ; => 6

; Nested let
(let ((x 10))
  (let ((y 20))
    (+ x y)))             ; => 30
```

### letrec - Recursive Bindings
```scheme
; Mutually recursive functions
(letrec ((even? (lambda (n)
                  (if (= n 0) t (odd? (- n 1)))))
         (odd? (lambda (n)
                 (if (= n 0) nil (even? (- n 1))))))
  (even? 10))             ; => t
```

### if - Conditional
```scheme
(if condition then-expr else-expr)

(if (> x 0)
    'positive
    'non-positive)
```

### quote - Quote Expression
```scheme
(quote (1 2 3))    ; => (1 2 3)
'(1 2 3)           ; shorthand
'foo               ; => foo (symbol)
```

### quasiquote - Template with Unquote
```scheme
; Basic quasiquote
(quasiquote (a b c))       ; => (a b c)
`(a b c)                   ; shorthand

; Unquote - evaluate within quasiquote
(let ((x 10))
  `(a ,x c))               ; => (a 10 c)

; Unquote-splicing - splice list
(let ((xs '(1 2 3)))
  `(a ,@xs b))             ; => (a 1 2 3 b)
```

### and / or - Short-Circuit Logic
```scheme
(and expr1 expr2 ...)      ; returns first falsy or last value
(or expr1 expr2 ...)       ; returns first truthy or nil

(and (> 5 3) (< 2 4))      ; => t
(or nil nil 42)            ; => 42
```

### do - Sequence Expressions
```scheme
(do
  expr1
  expr2
  ...
  exprN)                   ; returns exprN

(do
  (trace 'first)
  (trace 'second)
  42)                      ; => 42 (after printing traces)
```

---

## Pattern Matching

### match - Pattern Matching
```scheme
(match value
  (pattern1 result1)
  (pattern2 result2)
  ...)
```

### Pattern Types

#### Wildcard
```scheme
(match x
  (_ 'anything))           ; matches anything
```

#### Variable
```scheme
(match x
  (y (+ y 1)))             ; binds x to y
```

#### Literal
```scheme
(match x
  ((0) 'zero)
  ((1) 'one)
  (_ 'other))
```

#### Constructor
```scheme
(match pair
  ((cons a b) (+ a b)))
```

#### Nested
```scheme
(match nested
  ((cons (cons a b) c)
   (+ a (+ b c))))
```

#### Or-Pattern
```scheme
(match x
  ((or (0) (1)) 'zero-or-one)
  (_ 'other))
```

#### As-Pattern
```scheme
(match x
  ((y @ (cons a b))        ; bind whole to y, parts to a, b
   (list y a b)))
```

#### List Pattern
```scheme
(match lst
  ((list a b . rest)       ; match first two, rest is tail
   rest))
```

#### Guard
```scheme
(match x
  ((n :when (> n 0)) 'positive)
  ((n :when (< n 0)) 'negative)
  (_ 'zero))
```

---

## Primitives

### Arithmetic
| Operator | Description | Example |
|----------|-------------|---------|
| `+` | Addition | `(+ 1 2)` => 3 |
| `-` | Subtraction | `(- 10 3)` => 7 |
| `*` | Multiplication | `(* 4 5)` => 20 |
| `/` | Division | `(/ 20 4)` => 5 |
| `%` | Modulo | `(% 17 5)` => 2 |

### Comparison
| Operator | Description | Example |
|----------|-------------|---------|
| `=` | Equality | `(= 1 1)` => t |
| `<` | Less than | `(< 1 2)` => t |
| `>` | Greater than | `(> 2 1)` => t |
| `<=` | Less or equal | `(<= 1 1)` => t |
| `>=` | Greater or equal | `(>= 2 2)` => t |
| `not` | Logical not | `(not nil)` => t |

### List Operations
| Function | Description | Example |
|----------|-------------|---------|
| `cons` | Construct pair | `(cons 1 '(2 3))` => (1 2 3) |
| `car` / `fst` | First element | `(car '(1 2 3))` => 1 |
| `cdr` / `snd` | Rest of list | `(cdr '(1 2 3))` => (2 3) |
| `null?` | Is nil? | `(null? '())` => t |
| `list` | Make list | `(list 1 2 3)` => (1 2 3) |
| `length` | List length | `(length '(1 2 3))` => 3 |
| `append` | Concatenate | `(append '(1 2) '(3 4))` => (1 2 3 4) |
| `reverse` | Reverse list | `(reverse '(1 2 3))` => (3 2 1) |

### Higher-Order Functions
```scheme
; map - apply function to each element
(map (lambda (x) (* x 2)) '(1 2 3))    ; => (2 4 6)

; filter - keep elements matching predicate
(filter (lambda (x) (> x 2)) '(1 2 3 4 5))  ; => (3 4 5)

; fold / foldr - right fold
(fold + 0 '(1 2 3 4))      ; => 10

; foldl - left fold
(foldl - 0 '(1 2 3))       ; => -6

; apply - apply function to argument list
(apply + '(1 2 3 4))       ; => 10
```

### Function Combinators
```scheme
; compose - function composition (f ∘ g)
(let ((f (lambda (x) (+ x 1)))
      (g (lambda (x) (* x 2))))
  ((compose f g) 5))       ; => 11 (f(g(5)) = f(10) = 11)

; flip - swap argument order
(let ((sub (lambda (a b) (- a b))))
  ((flip sub) 1 10))       ; => 9 (10 - 1)
```

### Introspection
```scheme
(gensym)           ; generate unique symbol: g1, g2, ...
(gensym 'x)        ; with prefix: x1, x2, ...
(sym-eq? 'a 'a)    ; symbol equality: t
(eval '(+ 1 2))    ; evaluate quoted expression: 3
(trace expr)       ; print and return value
```

---

## Staging (Tower of Interpreters)

Purple implements a "tower of interpreters" where each level can generate code for the level below.

### lift - Quote Value as Code
```scheme
(lift 42)          ; => Code: mk_int(42)
(lift 'foo)        ; => Code: mk_sym("foo")
```

### run - Execute Code
```scheme
; Execute code value at base level
; Uses JIT compilation if gcc is available
(run (lift 42))    ; => 42
```

### EM - Escape to Meta-Level
```scheme
; Evaluate expression at parent interpreter level
(EM (+ 1 2))       ; => evaluated at meta-level
```

### shift - Go Up N Levels
```scheme
(shift 1 expr)     ; evaluate at 1 level up
(shift 2 expr)     ; evaluate at 2 levels up
```

### meta-level - Get Current Level
```scheme
(meta-level)       ; => 0 (at base level)
(EM (meta-level))  ; => 1 (at first meta-level)
```

### clambda - Compile Lambda
```scheme
; Compile lambda under current semantics
(clambda (x) (+ x 1))
```

---

## Handler Customization

The evaluator uses 9 handlers that can be customized:
- `lit` - literal evaluation
- `var` - variable lookup
- `lam` - lambda creation
- `app` - function application
- `if` - conditional
- `lft` - lift operation
- `run` - code execution
- `em` - meta-level jump
- `clam` - compiled lambda

### get-meta - Get Handler
```scheme
(get-meta 'lit)    ; => current lit handler
```

### set-meta! - Install Handler
```scheme
; Returns new menv with modified handler
(set-meta! 'lit (lambda (x) (* x 2)))
```

### with-handlers - Scoped Handler Changes
```scheme
(with-handlers
  ((lit (lambda (x) (* x 2)))
   (var (lambda (v) ...)))
  body)
```

### default-handler - Delegate to Default
```scheme
; Call default behavior from custom handler
(with-handlers
  ((lit (lambda (x)
          (+ 100 (default-handler 'lit x)))))
  (+ 1 2))         ; => 203 (101 + 102)
```

### with-menv - Evaluate with Custom Environment
```scheme
(with-menv custom-menv body)
```

---

## Macro System

### defmacro - Define Macro
```scheme
; (defmacro name (params...) body scope)
(defmacro when (cond body)
  `(if ,cond ,body nil)
  ; scope - code using the macro
  (mcall when (> 5 3) 'yes))  ; => yes
```

### mcall - Call Macro
```scheme
(mcall macro-name arg1 arg2 ...)

; Arguments are passed as unevaluated ASTs
(defmacro double (x)
  `(+ ,x ,x)
  (mcall double (+ 1 2)))  ; => 6, not 4
                           ; expands to (+ (+ 1 2) (+ 1 2))
```

### macroexpand - Expand Without Evaluating
```scheme
(defmacro inc (x)
  `(+ 1 ,x)
  (macroexpand (mcall inc y)))  ; => (+ 1 y)
```

### Common Macro Patterns

```scheme
; Conditional execution
(defmacro when (cond body)
  `(if ,cond ,body nil)
  ...)

; Local binding shorthand
(defmacro with-x (val body)
  `(let ((x ,val)) ,body)
  (mcall with-x 10 (+ x 5)))  ; => 15

; Multiple body expressions
(defmacro progn exprs
  `(do ,@exprs)
  ...)
```

---

## FFI (Foreign Function Interface)

### ffi - Call External Function
```scheme
; In code generation mode
(ffi "puts" "hello")
(ffi "printf" "%d\n" 42)

; Built-in functions work in interpreter mode:
(ffi "puts" str)      ; print string
(ffi "putchar" ch)    ; print character
(ffi "getchar")       ; read character
(ffi "exit" code)     ; exit program
```

### ffi-declare - Declare External Function
```scheme
(ffi-declare "int" "my_func" "int" "char*")
; Registers: int my_func(int, char*)
```

---

## Error Handling

### error - Raise Error
```scheme
(error 'something-went-wrong)
```

### try - Catch Errors
```scheme
(try
  risky-expression
  (lambda (err)
    (handle-error err)))
```

### assert - Conditional Error
```scheme
(assert (> x 0))                    ; fails if x <= 0
(assert (> x 0) 'must-be-positive)  ; with message
```

---

## Examples

### Factorial
```scheme
(letrec ((fact (lambda (n)
                 (if (= n 0)
                     1
                     (* n (fact (- n 1)))))))
  (fact 5))  ; => 120
```

### Fibonacci
```scheme
(lambda self (n)
  (if (< n 2)
      n
      (+ (self (- n 1))
         (self (- n 2)))))
```

### List Processing
```scheme
; Sum of squares
(fold + 0 (map (lambda (x) (* x x)) '(1 2 3 4 5)))  ; => 55

; Filter and map
(map (lambda (x) (* x 2))
     (filter (lambda (x) (> x 2)) '(1 2 3 4 5)))  ; => (6 8 10)
```

### Staging Example
```scheme
; Generate specialized code
(let ((multiplier (lift 3)))
  `(lambda (x) (* ,multiplier x)))
; => Code for (lambda (x) (* 3 x))
```

### Custom Handler Example
```scheme
; Double all literals
(with-handlers
  ((lit (lambda (x) (* x 2))))
  (+ 3 4))  ; => 14 (6 + 8)
```

### Macro Example
```scheme
; Define a threading macro
(defmacro -> (val . forms)
  (if (null? forms)
      val
      `(-> (,(car (car forms)) ,val ,@(cdr (car forms)))
           ,@(cdr forms)))
  ; Use it:
  (mcall -> 5
    (+ 3)      ; (+ 5 3) = 8
    (* 2)))    ; (* 8 2) = 16
```
