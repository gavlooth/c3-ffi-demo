# Type System Syntax Proposal

This document proposes syntax for completing the Julia-style type system
and optionally extending to higher-kinded types.

## Design Principles

1. **Reuse existing constructs** - quote, metadata maps, define attributes
2. **Type position vs value position** - context determines interpretation
3. **Minimal new tokens** - no special syntax like `{T}` for generics
4. **Lisp-like** - parentheses for application, including type application
5. **Flat metadata** - bounds inline in metadata dict, explicit grouping only when ambiguous

---

## Level 1: Julia Core (Current Target)

### 1.1 Type Annotations

```lisp
; Simple type annotation
^Int x
^String name

; Compound types use parentheses
^(List Int) items
^(Dict String Int) cache
^(Lambda Int Int) successor      ; function type
```

### 1.2 Function Types

Use `Lambda` to match value-level syntax:

```lisp
; Value level
(lambda (x) (* x 2))

; Type level
^(Lambda Int Int) inc            ; Int -> Int
^(Lambda Int Int Int) add        ; Int -> Int -> Int (curried)
^(Lambda A B) fn                 ; polymorphic
```

### 1.3 Abstract Type Hierarchy

```lisp
; Root abstract type
(define [abstract] Number)

; With parent
(define [abstract] Real Number)
(define [abstract] Integer Real)
```

### 1.4 Concrete Struct Types

```lisp
; Simple struct
(define [type] Point
  (^Float x)
  (^Float y))

; Inheritance
(define [type] Point3D Point
  (^Float z))
```

### 1.5 Parametric Types

```lisp
; Single parameter
(define [type] (Box T)
  (^T value))

; Multiple parameters
(define [type] (Pair A B)
  (^A first)
  (^B second))

; Type application in annotations
^(Box Int) intBox
^(Pair String Int) entry
```

**Construction** — infer type from arguments:

```lisp
(Box 42)                    ; infers Box{Int}
(Pair "key" 42)             ; infers Pair{String, Int}
(^(Box Int) (Box 42))       ; explicit when needed
```

### 1.6 Type Constraints

Bounds are specified directly in metadata (flat by default):

```lisp
; Single constraint - flat in metadata
^{'T Number}                            ; T <: Number

; With type annotation
^{'type (Lambda T T) 'T Number}         ; type + bound

; Multiple constraints
^{'T Number 'U Comparable}

; On a method
(define [method] ^{'T Number}
  double (^T x)
  (+ x x))

; Constraint on parametric type
(define [type] ^{'T Comparable}
  (SortedSet T)
  (^(List T) items))
```

**Explicit `'with` only when ambiguous** (rare):

```lisp
; If type variable conflicts with metadata key
^{'type (Lambda pure pure) 'with {'pure Number} 'pure true}

; For visual grouping (optional style choice)
^{'type (Lambda T U V)
  'with {'T Number 'U Comparable 'V T}}
```

### 1.7 Union Types / Sum Types

```lisp
; Simple union (enum-like)
(define [union] Bool True False)

; Parametric union
(define [union] (Option T)
  None
  (Some T))

; Multiple type parameters
(define [union] (Either E A)
  (Left E)
  (Right A))

; Construction
None                  ; nullary variant
(Some 42)             ; variant with value

; Pattern matching
(match opt
  (None default)
  ((Some x) (use x)))
```

### 1.8 Type Aliases

```lisp
; Simple alias
(define [alias] Text String)

; Parametric alias
(define [alias] (Maybe T) (Option T))

; Complex alias
(define [alias] (Result T) (Either String T))
```

---

## Level 2: Advanced Julia Features

### 2.1 Tuple Types

```lisp
; Tuple type annotation
^(Tuple Int String Bool) triple

; Tuple literal
[1 "hello" true]      ; inferred as Tuple{Int, String, Bool}
```

### 2.2 Union Type Expressions (Inline)

```lisp
; Nullable int
^(Union Int Nothing) maybeInt

; Multiple types
^(Union Int String) intOrString
```

### 2.3 Variance Annotations

```lisp
; Covariant (output position)
(define [type] ^{'covariant 'T}
  (Producer T)
  ...)

; Contravariant (input position)
(define [type] ^{'contravariant 'T}
  (Consumer T)
  ...)
```

---

## Level 3: Higher-Kinded Types (Extension)

### 3.1 HKT Arity

Instead of Haskell's `* -> *` notation, use **arity numbers**:

```lisp
; F takes 1 type argument (like List, Option)
^{'F 1}

; M takes 2 type arguments (like Either, Dict)
^{'M 2}

; Mixed with type bounds
^{'T Number 'F 1}               ; T <: Number, F takes 1 arg
```

**Inference**: Arity is usually inferred from usage:

```lisp
(define [trait] (Functor F)
  ; F used as (F A), (F B) → infer F takes 1 arg
  (fmap ^(Lambda (Lambda A B) (F A) (F B))))
```

Explicit arity only when needed:

```lisp
^{'F 1}                         ; explicit: F takes 1 type arg
```

### 3.2 Traits (Typeclasses)

Use `[trait]` attribute:

```lisp
(define [trait] (Functor F)
  (fmap ^(Lambda (Lambda A B) (F A) (F B))))

(define [trait] (Monad M)
  ^{'extends Functor}
  (pure ^(Lambda A (M A)))
  (bind ^(Lambda (M A) (Lambda A (M B)) (M B))))
```

### 3.3 Instances

Use `[instance]` attribute (clear terminology):

```lisp
(define [instance] (Functor Option)
  (define (fmap f opt)
    (match opt
      (None None)
      ((Some x) (Some (f x))))))

(define [instance] (Functor List)
  (define (fmap f xs) (map f xs)))

(define [instance] (Monad Option)
  (define (pure x) (Some x))
  (define (bind opt f)
    (match opt
      (None None)
      ((Some x) (f x)))))
```

### 3.4 Using HKT Constraints

```lisp
(define [method] ^{'F 1}
  double-all (^(F Int) container)
  (fmap (lambda (x) (* 2 x)) container))

; With Functor constraint (if we track it)
(define [method] ^{'F Functor}
  double-all (^(F Int) container)
  (fmap (lambda (x) (* 2 x)) container))
```

---

## Metadata Value Disambiguation

When metadata is flat, the **value type** determines meaning:

| Value Type | Meaning | Example |
|------------|---------|---------|
| Type name (symbol) | Type bound | `'T Number` → T <: Number |
| Integer | HKT arity | `'F 1` → F takes 1 type arg |
| Boolean | Metadata flag | `'pure true` |
| String | Metadata info | `'doc "description"` |

```lisp
; All flat, unambiguous
^{'type (Lambda T T) 'T Number 'pure true 'inline true}
;        ↑ type        ↑ bound   ↑ flag    ↑ flag
```

---

## Summary: Final Syntax

| Feature | Syntax | Example |
|---------|--------|---------|
| Type annotation | `^Type` | `^Int x` |
| Compound type | `^(Ctor Args)` | `^(List Int) xs` |
| Function type | `^(Lambda Args Ret)` | `^(Lambda Int Int) f` |
| Type bound | `'T Type` (flat) | `^{'T Number}` |
| Multiple bounds | `'T Type 'U Type` | `^{'T Number 'U Eq}` |
| HKT arity | `'F n` | `^{'F 1}` |
| Explicit grouping | `'with {...}` | `^{'with {'T Number}}` |
| Abstract type | `(define [abstract] ...)` | `(define [abstract] Number)` |
| Struct type | `(define [type] ...)` | `(define [type] Point ...)` |
| Parametric type | `(define [type] (Name T) ...)` | `(define [type] (Box T) ...)` |
| Union type | `(define [union] ...)` | `(define [union] (Option T) ...)` |
| Type alias | `(define [alias] ...)` | `(define [alias] Text String)` |
| Trait | `(define [trait] ...)` | `(define [trait] (Functor F) ...)` |
| Instance | `(define [instance] ...)` | `(define [instance] (Functor Option) ...)` |

---

## Complete Example

```lisp
; Abstract hierarchy
(define [abstract] Number)
(define [abstract] Real Number)

; Parametric struct
(define [type] (Box T)
  (^T value))

; Union type
(define [union] (Option T)
  None
  (Some T))

; Method with type bound
(define [method] ^{'T Number}
  double (^T x)
  (+ x x))

; Trait definition
(define [trait] (Functor F)
  (fmap ^(Lambda (Lambda A B) (F A) (F B))))

; Trait instance
(define [instance] (Functor Option)
  (define (fmap f opt)
    (match opt
      (None None)
      ((Some x) (Some (f x))))))

; Using the trait
(fmap (lambda (x) (* 2 x)) (Some 21))  ; => (Some 42)
```

---

## Implementation Roadmap

### Phase 1: Connect Types to Runtime ✓ COMPLETE
- [x] Wire TypeRegistry to Interp
- [x] infer_value_type for all 16 ValueTag types
- [x] Type checking in apply()
- [x] Register type names from eval_deftype/eval_abstract
- [x] Trait definition with `[trait]` attribute
- [x] Instance definition with `[instance]` attribute
- [x] InstanceRegistry for method lookup
- [x] Flat metadata bound parsing

### Phase 2: Parametric Types (IN PROGRESS)
- [x] Type parameter storage in ParametricType
- [ ] Type substitution algorithm
  - Replace type variables with concrete types
  - `(Box T)` + `T=Int` → `(Box Int)`
- [ ] Unification for inference
  - Bidirectional type inference
  - Constraint generation and solving
- [ ] Instantiation at call sites
  - Infer type arguments from value arguments
  - `(Box 42)` infers `Box{Int}`

### Phase 3: Full HKT Dispatch
- [x] Arity notation (`'F 1` for `* -> *`)
- [x] Trait definitions stored in TypeRegistry
- [x] Instance registration in InstanceRegistry
- [ ] Method dispatch based on runtime type
  - `(fmap f (Some 42))` → lookup `(Functor Option).fmap`
  - Use `infer_value_type` to get implementing type
- [ ] Typeclass constraint checking
  - Verify `F` has `Functor` instance before applying `fmap`

### Phase 4: Advanced Features
- [ ] Variance annotations
  - `^{'covariant 'T}` for output positions
  - `^{'contravariant 'T}` for input positions
- [ ] Associated types
  - `(define [trait] (Iterable C) (type Item) ...)`
- [ ] Default method implementations
  - Trait methods with default bodies

---

## Next Implementation Steps

### Immediate: Type Substitution

```c3
// In types.c3, add:
struct Substitution {
    SymbolId[16] vars;
    TypeId[16] types;
    usz count;
}

fn TypeId apply_substitution(TypeId type, Substitution* subst) {
    // If type is a type variable, look it up in subst
    // If type is parametric, recursively substitute parameters
    // Otherwise return type unchanged
}
```

### Then: Method Dispatch

```c3
// In eval.c3, modify function application:
fn Value* dispatch_trait_method(SymbolId method_name, Value* arg, Interp* interp) {
    // 1. Get runtime type of arg
    TypeId arg_type = infer_value_type(arg, interp.types);

    // 2. Find trait that defines this method
    // 3. Look up instance for arg_type
    // 4. Get method implementation from instance
    // 5. Apply method to arg
}
```

### Syntax for Parametric Instantiation

```lisp
; Explicit type application (when inference fails)
((Box ^Int) 42)               ; explicitly Box{Int}
((Either ^String ^Int) ...)   ; Either{String, Int}

; Usually inferred from arguments
(Box 42)                      ; inferred Box{Int}
(Pair "key" 42)               ; inferred Pair{String, Int}
```
