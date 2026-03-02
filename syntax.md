# Lisp Syntax Reference

This document describes the complete syntax for the Lisp dialect with type annotations,
multiple dispatch, and delimited continuations.

## Core Syntax

### Literals

```lisp
; Numbers
42          ; integer
-10         ; negative integer
3.14        ; float
-2.5        ; negative float
1.0e-5      ; scientific notation

; Strings
"hello"           ; simple string
"hello\nworld"    ; with newline escape
"say \"hi\""      ; with escaped quotes

; Other
x           ; symbol (variable reference)
'symbol     ; quoted symbol (for dict keys, named args)
()          ; nil (empty list)
'(1 2 3)    ; quoted list
```

### Lambda Expressions

```lisp
; Untyped lambda (single parameter for now)
(lambda (x) body)

; Typed lambda with Clojure-style ^ metadata
(lambda (^Int x) body)
(lambda (^Int x ^Float y) body)

; Typed lambda with return type annotation
(lambda ^Int (^Int x) x)
```

### Control Flow

```lisp
; Conditional
(if test then-expr else-expr)

; Let binding
(let ((x value)) body)
```

### Destructuring

Destructuring allows binding multiple values from compound data structures.

#### Array Destructuring

```lisp
; Bind elements by position
(let (([a b c] arr))
  (+ a b c))

; Rest binding with .. (captures remaining elements)
(let (([head .. tail] items))
  (cons (process head) tail))

; Ignore rest (take only first elements)
(let (([x y ..] long-list))
  (+ x y))

; Take only last element
(let (([.. last] items))
  last)

; Nested destructuring
(let (([[a b] [c d]] matrix))
  (+ a b c d))
```

#### Dictionary Destructuring

```lisp
; Extract keys by name (using quoted symbols)
(let (({'keys [name age]} person))
  (format "~a is ~a years old" name age))

; Multiple keys
(let (({'keys [x y z]} point3d))
  (sqrt (+ (* x x) (* y y) (* z z))))
```

#### Ignore Pattern

```lisp
; Use _ to ignore a binding
(let (([_ second _] triple))
  second)
```

### Named Arguments

Function calls can use quoted symbols for named arguments:

```lisp
; Named arguments at call site
(make-point 'x 1 'y 2)

; Order doesn't matter with named args
(create-user 'age 25 'name "Alice")

; Expressions as values
(draw-rect 'width (+ w 10) 'height (* h 2))
```

> **Note:** Named arguments use quoted symbols (`'name`) to identify parameters.

### Pattern Matching

```lisp
; Match on union types
(match expr
  (None default-value)           ; nullary constructor
  ((Some x) x)                   ; constructor with binding
  ((Pair a b) (+ a b))           ; multiple bindings
  (x x))                         ; variable/wildcard pattern

; Pattern types:
;   UpperCase     -> nullary constructor (None, True, Nil)
;   (Ctor x y)    -> constructor with bindings
;   lowercase     -> variable pattern (binds any value)
```

### Quoting

```lisp
(quote datum)     ; full form
'datum            ; shorthand
```

Quoted symbols serve dual purposes:
- As literal symbols in expressions: `'foo`
- As dictionary keys and named argument identifiers

## Unified Define Syntax

All definitions use the unified `define` form with optional `[]` attributes.

### Value Definitions

```lisp
; Simple value
(define x 42)

; Typed value
(define ^Int x 42)
```

### Function Definitions (Scheme-style)

```lisp
; Untyped function
(define (f x) body)

; Function with typed parameters
(define (f ^Int x ^Int y) body)

; Function with return type annotation
(define (^Int f ^Int x ^Int y) body)
```

### Method Definitions (Multiple Dispatch)

Methods are defined with the `[method]` attribute and are dispatched
based on runtime types of arguments.

```lisp
; Basic method
(define [method] add (^Int x ^Int y)
  (+ x y))

; Method with return type
(define [method] ^Int add (^Int x ^Int y)
  (+ x y))

; Compound attributes (inline method)
(define [method inline] add (^Int x ^Int y)
  (+ x y))
```

### Type Definitions

```lisp
; Struct type
(define [type] Point
  (^Float x)
  (^Float y))

; Struct with parent type (inheritance)
(define [type] Point3D Point
  (^Float z))
```

### Abstract Types

Abstract types define type hierarchies without fields.

```lisp
; Abstract base type
(define [abstract] Number)

; Abstract type with parent
(define [abstract] Real Number)
(define [abstract] Integer Real)
```

### Union Types

Union types (sum types) define types with variants:

```lisp
; Simple union
(define [union] Result Ok Error)

; Parametric union with variants
(define [union] (Option T)
  None
  (Some T))

; Multiple type parameters
(define [union] (Either A B)
  (Left A)
  (Right B))
```

### Type Aliases

Type aliases create new names for existing types:

```lisp
; Simple alias
(define [alias] String CharArray)

; Parametric alias
(define [alias] (Vec T) (Array T))

; Multiple parameters
(define [alias] (Pair A B) (Tuple A B))
```

### Path Expressions (Field Access)

Dot notation for accessing fields:

```lisp
point.x                  ; access field x of point
person.address.city      ; nested field access

; In expressions
(+ point.x point.y)
(set! counter.value (+ counter.value 1))
```

### Bracket Indexing (Array/Dictionary Access)

Bracket notation for accessing elements by index or key.

```lisp
; Array/list indexing by position
arr[0]                   ; first element
arr[-1]                  ; last element (negative indexing)
arr[i]                   ; variable index
arr[(+ i 1)]             ; expression index

; Chained indexing for matrices
matrix[i][j]             ; 2D access

; Dictionary access (using quoted symbols)
dict['key]               ; quoted symbol key (preferred)
dict["key"]              ; string key

; Indexing function results
(make-array 10)[0]       ; index into returned value

; In expressions
(+ arr[0] arr[1])
(set! arr[i] value)
```

> **Note:** Quoted symbols (`'key`) are the preferred way to access dictionary entries.

## Type Annotations

Type annotations use Clojure-style `^Type` metadata prefix.

```lisp
; Annotate any expression
^Int 42
^Point (make-point 1.0 2.0)

; In parameter lists
(lambda (^Int x ^Float y) ...)
(define [method] foo (^String s) ...)
```

## Metadata Maps

Extended metadata uses `^{...}` brace-delimited maps with quoted symbol keys.
Type bounds are specified **flat** in the metadata (no nesting required):

```lisp
; Type constraint - flat in metadata
(define [method] ^{'T Number}
  double (^T x)
  (+ x x))

; Multiple constraints - flat
(define [method] ^{'T Number 'U Comparable}
  convert (^T x ^U y)
  ...)

; With explicit type annotation
^{'type (Lambda T T) 'T Number}

; On parametric types
(define [type] ^{'T Eq}
  (Set T)
  (^(List T) items))

; Flags (no value = true)
(define [type] ^{'mutable true}
  Counter
  (^Int value))

; Explicit 'with grouping (only when ambiguous)
^{'type (Lambda T U V)
  'with {'T Number 'U Comparable 'V T}}
```

### Metadata Value Disambiguation

The **value type** determines the meaning of each entry:

| Value Type | Meaning | Example |
|------------|---------|---------|
| Type name (symbol) | Type bound | `'T Number` → T <: Number |
| Integer | HKT arity | `'F 1` → F takes 1 type arg |
| Boolean | Metadata flag | `'pure true` |
| String | Metadata info | `'doc "description"` |

### Metadata Syntax

| Form | Meaning | Example |
|------|---------|---------|
| `^TypeName` | Simple type hint | `^Int`, `^String` |
| `^flag` | Bare modifier flag | `^rec`, `^strict`, `^key`, `^index` |
| `^{...}` | Brace-delimited metadata map | `^{'T Number 'pure true}` |
| `'T TypeName` | Type bound (inside dict) | `'T Number` → T <: Number |
| `'F n` | HKT arity (inside dict) | `'F 1` → F takes 1 type arg |
| `'key true` | Boolean flag (inside dict) | `'mutable true` |
| `'with {...}` | Explicit grouping | when ambiguous |

`^` is a **general metadata mechanism**: types, flags, and dict metadata.
Bare `^hint` works like `^rec`/`^strict` (modifier flags).
Dict `^{'key value}` works for richer metadata (type bounds, multiple properties).
Both forms stack: `(x ^Int ^key)` = typed + flagged.

## Delimited Continuations

### Reset/Shift

```lisp
; Delimit continuation
(reset body)

; Capture continuation as k
(shift k body)
```

### Effect Handlers

```lisp
; Perform an effect
(perform effect-tag argument)

; Handle effects
(handle body
  ((effect-tag k arg) handler-body)
  ...)
```

## Attribute List

Attributes are specified in `[]` brackets after `define`:

| Attribute   | Meaning                              |
|-------------|--------------------------------------|
| `method`    | Multiple dispatch method             |
| `type`      | Struct type definition               |
| `abstract`  | Abstract type (no fields)            |
| `union`     | Union/sum type definition            |
| `alias`     | Type alias definition                |
| `inline`    | Hint for inlining (can be combined)  |

Attributes can be combined:
```lisp
(define [method inline] fast-add (^Int x ^Int y) ...)
```

## Legacy Syntax (Deprecated)

These forms are still supported but the unified `define` syntax is preferred:

```lisp
; Legacy method
(defmethod add (^Int x ^Int y) body)

; Legacy type
(deftype Point (^Float x) (^Float y))

; Legacy abstract
(abstract Number)
(abstract Real Number)
```

## Function Types

Function types use `Lambda` to match the value-level syntax:

```lisp
; Value level
(lambda (x) (* x 2))

; Type level
^(Lambda Int Int) inc            ; Int -> Int
^(Lambda Int Int Int) add        ; Int -> Int -> Int (curried)
^(Lambda A B) fn                 ; polymorphic
```

## Traits and Instances (Higher-Kinded Types)

### Trait Definition

```lisp
(define [trait] (Functor F)
  (fmap ^(Lambda (Lambda A B) (F A) (F B))))

(define [trait] (Monad M)
  ^{'extends Functor}
  (pure ^(Lambda A (M A)))
  (bind ^(Lambda (M A) (Lambda A (M B)) (M B))))
```

### Trait Instance

```lisp
(define [instance] (Functor Option)
  (define (fmap f opt)
    (match opt
      (None None)
      ((Some x) (Some (f x))))))

(define [instance] (Monad Option)
  (define (pure x) (Some x))
  (define (bind opt f)
    (match opt
      (None None)
      ((Some x) (f x)))))
```

### HKT Arity

Higher-kinded type parameters use arity numbers (not Haskell's `* -> *`):

```lisp
^{'F 1}                         ; F takes 1 type arg (like List, Option)
^{'M 2}                         ; M takes 2 type args (like Either)
^{'T Number 'F 1}               ; T <: Number, F takes 1 arg
```

## Feature Summary

| Feature | Syntax | Example |
|---------|--------|---------|
| Array destructuring | `[a b c]` | `(let (([a b c] arr)) ...)` |
| Rest binding | `[head .. tail]` | `(let (([x .. rest] list)) ...)` |
| Dict destructuring | `{'keys [k1 k2]}` | `(let (({'keys [name]} obj)) ...)` |
| Named arguments | `'key value` | `(f 'x 1 'y 2)` |
| Pattern matching | `(match ...)` | `(match opt (None 0) ((Some x) x))` |
| Field access | `obj.field` | `point.x` |
| Bracket indexing | `arr[i]` | `matrix[0][1]` |
| Function type | `^(Lambda A B)` | `^(Lambda Int Int) f` |
| Type bound | `'T Type` | `^{'T Number}` |
| HKT arity | `'F n` | `^{'F 1}` |
| Trait | `(define [trait] ...)` | `(define [trait] (Functor F) ...)` |
| Instance | `(define [instance] ...)` | `(define [instance] (Functor Option) ...)` |

## Design Note: Quoted Symbols vs Keywords

This dialect uses **quoted symbols** (`'foo`) instead of Clojure-style keywords (`:foo`).

**Rationale:**
- Quote is a fundamental Lisp mechanism already present in the language
- Eliminates a separate token type and evaluation rule
- `'foo` naturally produces a symbol value usable as a dictionary key
- Simplifies both the parser and evaluator
- Maintains visual distinction from variable references

**Usage:**
```lisp
; Dictionary literal
{'name "Alice" 'age 30}

; Dictionary access
person['name]

; Named function arguments
(make-point 'x 10 'y 20)

; Metadata maps
^{'where [T Number]}
```

## Complete Examples

```lisp
; Define an abstract numeric hierarchy
(define [abstract] Number)
(define [abstract] Real Number)
(define [abstract] Integer Real)

; Define a concrete point type
(define [type] Point
  (^Float x)
  (^Float y))

; Define methods for addition (multiple dispatch)
(define [method] add (^Int a ^Int b)
  (+ a b))

(define [method] add (^Float a ^Float b)
  (+ a b))

(define [method] add (^Point p1 ^Point p2)
  (make-point
    (+ (point-x p1) (point-x p2))
    (+ (point-y p1) (point-y p2))))

; Define a simple function
(define (square x)
  (* x x))

; Define a typed function
(define (^Int factorial ^Int n)
  (if (= n 0)
      1
      (* n (factorial (- n 1)))))

; Use delimited continuations
(define (example)
  (reset
    (+ 1 (shift k (k (k 10))))))
; Returns 12: (+ 1 (+ 1 10))

; Destructuring examples
(define (sum-triple triple)
  (let (([a b c] triple))
    (+ a b c)))

(define (first-and-rest items)
  (let (([head .. tail] items))
    (list head tail)))

; Named arguments
(define (create-point)
  (make-point 'x 10 'y 20))

; Combined: destructure with pattern matching
(define (process-result result)
  (match result
    ((Ok value)
      (let (({'keys [data status]} value))
        (list status data)))
    ((Err msg) msg)))
```
