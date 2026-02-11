# Julia-Style Type System - Syntax Proposal

---

## 1. Parametric Types ✓

Scheme-style with parameters in name position:

```lisp
(define [type] (Point T)
  (^T x)
  (^T y))

(define [type] (Pair K V)
  (^K key)
  (^V value))

(define [type] (List T)
  (^T head)
  (^(Option (List T)) tail))
```

### Instantiation
```lisp
(Point Int)              ; type: Point{Int}
((Point Float) 1.0 2.0)  ; construct Point{Float}
```

---

## 2. Type Constraints ✓

Using `^{:where [...]}` metadata map with braces:

```lisp
; Single constraint - T must be subtype of Number
(define [method] ^{:where [T Number]}
  add (^T x ^T y)
  (+ x y))

; Multiple constraints as nested list
(define [method] ^{:where [[T Real] [U T]]}
  convert (^T x ^U target)
  ...)

; On parametric types
(define [type] ^{:where [T Number]}
  (NumericPair T)
  (^T first)
  (^T second))

; Combined metadata
(define [type] ^{:where [T Comparable], :mutable true}
  (SortedSet T)
  (^(List T) items))
```

### Metadata Syntax
- `^{...}` - brace-delimited metadata map
- `:keyword [value]` - keyword with list value
- `:keyword symbol` - keyword with symbol value
- `:keyword` - flag (implies true)

---

## 3. Union Types ✓

```lisp
(define [union] NumOrStr Int String)

(define [union] (Option T)
  None
  (Some T))

(define [union] (Result T E)
  (Ok T)
  (Err E))
```

---

## 4. Field Access (Path Expressions) ✓

Dot notation from OmniLisp:

```lisp
point.x
point.y
person.address.city

; In expressions
(+ point.x point.y)
(set! counter.value (+ counter.value 1))
```

---

## 4.1 Bracket Indexing ✓

Bracket notation for arrays, lists, and dictionaries:

```lisp
; Array/list indexing
arr[0]                   ; first element
arr[-1]                  ; last element
arr[i]                   ; variable index
matrix[i][j]             ; chained indexing

; Dictionary access
dict[:key]               ; keyword key (preferred)
dict["key"]              ; string key

; In expressions
(+ arr[0] arr[1])
(make-array 10)[0]       ; index function result
```

> Note: Keywords (`:key`) are the preferred way to access dictionary entries.

---

## 5. Mutable Types ✓

```lisp
(define [type mutable] Counter
  (^Int value))

(set! counter.value 10)
```

---

## 6. Type Aliases ✓

```lisp
(define [alias] Real Float)
(define [alias] StringList (List String))
(define [alias] (Vec T) (Array T))
```

---

## 7. Function Types ✓

Using `Proc` keyword:

```lisp
^(Proc Int Int)         ; Int -> Int
^(Proc Int Int Int)     ; (Int, Int) -> Int
^(Proc Int)             ; () -> Int

; Higher-order
^(Proc (Proc Int Int) Int)  ; (Int -> Int) -> Int

; In method signatures
(define [method] map (^(Proc A B) f ^(List A) xs) ...)
```

---

## 7.1 Pattern Matching ✓

```lisp
(match expr
  (None default)           ; nullary constructor
  ((Some x) x)             ; constructor with binding
  ((Pair a b) (+ a b))     ; multiple bindings
  (x x))                   ; variable pattern
```

---

## 8. Summary

| Feature | Syntax | Status |
|---------|--------|--------|
| Parametric types | `(define [type] (Point T) ...)` | ✓ |
| Constraints | `^{:where [T Number]}` | ✓ |
| Union types | `(define [union] Name ...)` | ✓ |
| Pattern matching | `(match expr ...)` | ✓ |
| Field access | `object.field` | ✓ |
| Bracket indexing | `arr[0]`, `dict[:key]` | ✓ |
| String literals | `"hello\nworld"` | ✓ |
| Float literals | `3.14`, `1.0e-5` | ✓ |
| Mutable | `[type mutable]` | ✓ |
| Aliases | `(define [alias] Name Type)` | ✓ |
| Function types | `^(Proc Int Int Int)` | ✓ |

---

## Memory Integration

```lisp
; Region allocation
(with-region r
  (define p (Point 1.0 2.0))
  (+ p.x p.y))

; Destructor
(define [type] ^{:destructor close-file}
  FileHandle
  (^Int fd))
```
