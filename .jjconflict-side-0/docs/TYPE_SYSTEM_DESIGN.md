# OmniLisp Type System Design: Flexible Dispatch with Static Safety

## Executive Summary

OmniLisp aims to combine the best of three worlds:
- **Lisp's runtime flexibility** - Programmer is in control
- **Julia's multiple dispatch** - Powerful, composable polymorphism
- **Static type safety** - Compile-time ambiguity detection where possible

**Key insight:** Ambiguity in multiple dispatch is **not** fundamentally undecidable. We can detect and resolve ambiguity at compile time using type lattice traversal algorithms, while preserving runtime flexibility for truly dynamic code.

---

## OmniLisp Syntax Primer (Character Calculus)

| Delimiter | Domain | Purpose |
|-----------|--------|---------|
| `{}` | Kind | Type annotations, blueprints |
| `[]` | Slot | Arguments, parameters, data |
| `()` | Flow | Execution, value construction |
| `^` | Metadata | Instructions, constraints |

**Function definition pattern:**
```omnilisp
(define name [param {Type}?] {ReturnType}? body)
```

**Type system features:**
- Parameters in Slots `[]`, types in Kinds `{}` (optional)
- Variance: `^:covar` or `^:covariant` (metadata on type parameters)
- Unions: `(union [{Int32} {String}])`
- Abstract types: `{abstract Name}`
- Structs: `{struct Name}`
- Parent: `^:parent {ParentType}` (metadata)

**Dispatch in OmniLisp:**
Multiple dispatch is achieved through **multiple `define` declarations** with the same name but different type constraints:

```omnilisp
;; Multiple defines = multiple dispatch methods
(define process [x {Integer}] {String}
  "integer method")

(define process [x {Float}] {String}
  "float method")

;; Compiler analyzes all definitions and selects most specific
```

**Match expressions:**
Pattern matching uses single `[]` brackets for pattern-result pairs:

## The Problem: Multiple Dispatch + Covariant Containers

### The Core Conflict

```omnilisp
;; Define type hierarchy
(define ^:parent {Any} {abstract Number} [])
(define ^:parent {Number} {abstract Integer} [])
(define ^:parent {Number} {abstract Float} [])

;; Define covariant container
(define {struct [^:covar T]} {Vector}
  [data {T}])

;; Now we have:
;; (Vector {Integer}) ⊑ (Vector {Number})  ✓  (covariant!)

;; Multiple dispatch via multiple defines:
(define foo [x {(Vector {Integer})}] {String}
  "integer method")

(define foo [x {(Vector {Number})}] {String}
  "number method")

;; Question: Which method for (Vector {Integer})?
;; Answer: First method (Integer is more specific than Number)
```

**The conflict:** Covariant subtyping creates a **lattice** of types, not a tree. Multiple dispatch over this lattice can have ambiguous cases where no unique "most specific" method exists.

### Concrete Ambiguity Example

```omnilisp
;; Ambiguous definitions - no fallback:
(define bar [x {(Vector {Integer})}] {Int32}
  1)

(define bar [x {(Vector {Float})}] {Int32}
  2)
  ;; No method for (Vector {Number})!

;; What if argument is (Vector {Number}) (abstract supertype)?
;; Both methods are applicable, neither is more specific
;; → Compile-time error OR explicit disambiguation needed
```

---

## Solution: Compile-Time Ambiguity Detection

### Type Lattice Construction

At compile time, we build a Directed Acyclic Graph (DAG) of subtype relationships:

```
              (Vector {Number})
                   |
        +----------+----------+
        |                     |
  (Vector {Float})         (Vector {Int})
        |                     |
        +----------+----------+
                   |
                (Vector {Any})
```

This lattice encodes:
- **Subtype relations:** T1 ⊑ T2 if there's a path from T1 to T2
- **Specificity:** T1 is more specific than T2 if T1 ⊑ T2
- **Joins:** The least upper bound of two types (their common ancestor)

### OmniLisp Syntax for Type Definitions

```omnilisp
;; Abstract types
(define ^:parent {Any} {abstract Number} [])
(define ^:parent {Number} {abstract Real} [])
(define ^:parent {Number} {abstract Integer} [])
(define ^:parent {Number} {abstract Float} [])

;; Primitive types (with bit width)
(define ^:parent {Integer} {primitive Int32} [32])
(define ^:parent {Float} {primitive Float64} [64])

;; Parametric types (structs)
(define {struct [^:covar T]} {List}
  [head {T} tail {(List T)}])

;; Invariant container (for mutable data)
(define {struct [^:invariant T]} {Array}
  [data {T} capacity {Int32}])

;; Union types
(define {Int32OrString} (union [{Int32} {String}]))
```

**Character Calculus Note:**
- `()` = Flow (execution, type construction): `(List T)` applies T to List
- `[]` = Slot (data, parameters): `[head T]` is a field with type
- `{}` = Kind (types, blueprints): `{T}` is the type T itself
- `^` = Metadata (instructions): `^:parent {Number}` sets parent

### Dispatch is Multiple `define` Declarations

```omnilisp
;; OmniLisp achieves multiple dispatch through multiple definitions:
(define compute [x {Int32}] {Int32}
  (* x 2))

(define compute [x {Float64}] {Float64}
  (* x 2.0))

(define compute [x {Number}] {Number}
  x)

;; Call: (compute 42) → selects Int32 version
;; Call: (compute 3.14) → selects Float64 version
```

The compiler collects all `define` forms with the same name and builds a dispatch table.

### Ambiguity Detection Algorithm

```
For each multimethod M with methods {m1, m2, ..., mn}:

1. Build type lattice L for all types in M's signatures

2. For each pair (mi, mj) where i ≠ j:

   a. Compute applicability regions:
      Ri = { t ∈ L | t matches signature(mi) }
      Rj = { t ∈ L | t matches signature(mj) }

   b. Compute intersection: Iij = Ri ∩ Rj

   c. If Iij = ∅:
      → No overlap, no ambiguity

   d. Else if mi ⊑ mj (mi is more specific):
      → mi covers the overlap, no ambiguity

   e. Else if mj ⊑ mi:
      → mj covers the overlap, no ambiguity

   f. Else:
      → AMBIGUITY at all types in Iij
      → Require disambiguation

3. Resolution strategies:
   a. Error out (strict)
   b. Require explicit :priority annotation
   c. Insert runtime checks (flexible)
   d. Prompt programmer for guidance

Complexity: O(n² × |L|) where n = number of methods
Optimization: Use lattice structure, memoization, early termination
```

### Implementation: Lattice Traversal

```lisp
;; Build the type lattice
(defun build-type-lattice (types)
  (let ((lattice (make-dag)))
    (dolist (t types)
      (add-node! lattice t)
      (dolist (parent (type-parents t))
        (add-edge! lattice t parent)))
    lattice))

;; Collect all ancestors (supertypes) of a type
(defun collect-ancestors (type lattice)
  (let ((ancestors (set)))
    (traverse-up lattice type
                 (lambda (t) (set-add! ancestors t)))
    ancestors))

;; Find Least Upper Bound (join) of two types
(defun find-join (t1 t2 lattice)
  (let ((ancestors1 (collect-ancestors t1 lattice))
        (ancestors2 (collect-ancestors t2 lattice)))
    ;; Find minimal elements in intersection
    (let ((common (set-intersection ancestors1 ancestors2)))
      (remove-if (lambda (t)
                   (some (lambda (other)
                           (and (not (eq t other))
                                (subtype? other t lattice)))
                         common))
                 common))))

;; Compute overlap of two method signatures
(defun compute-overlap (sig1 sig2 lattice)
  (let ((t1 (param-type sig1 0))
        (t2 (param-type sig2 0)))
    (cond
      ((subtype? t1 t2 lattice) t1)    ; T1 more specific
      ((subtype? t2 t1 lattice) t2)    ; T2 more specific
      (t (find-join t1 t2 lattice))))) ; Find LUB

;; Check if two methods are ambiguously overlapping
(defun ambiguous-p (m1 m2 lattice)
  (let ((overlap (compute-overlap (sig m1) (sig m2) lattice)))
    (and overlap
         (not (more-specific-p (sig m1) (sig m2) lattice))
         (not (more-specific-p (sig m2) (sig m1) lattice)))))
```

### Multi-Parameter Dispatch

The algorithm extends naturally to multiple parameters:

```lisp
;; For two methods with k parameters each:
;; Check overlap in EACH parameter dimension
;; Compute Cartesian product of overlaps
;; Ambiguity exists if any point in product space is ambiguous

(defun ambiguous-p-multi (m1 m2 lattice)
  (let ((params1 (sig-params (sig m1)))
        (params2 (sig-params (sig m2))))
    ;; Check if m1 is more specific in ALL parameters
    (let ((m1-more-specific?
           (every (lambda (p1 p2)
                    (subtype? p1 p2 lattice))
                  params1 params2))
          (m2-more-specific?
           (every (lambda (p1 p2)
                    (subtype? p2 p1 lattice))
                  params1 params2)))
      (not (or m1-more-specific? m2-more-specific?)))))
```

---

## OmniLisp Design: Maximum Flexibility + Safety

### Design Principles

1. **Covariant by default** - Immutable containers (Flow `()`) are covariant
2. **Invariant when mutable** - Mutable containers (Slot `[]`) are invariant
3. **Multiple dispatch** - Powerful polymorphism via pattern matching
4. **Compile-time checking** - Detect ambiguity when possible
5. **Runtime escape hatch** - Flexibility for dynamic code
6. **Programmer control** - Explicit disambiguation when needed

### Type Declarations in OmniLisp

```omnilisp
;; Abstract types
(define ^:parent {Any} {abstract Number} [])
(define ^:parent {Number} {abstract Integer} [])
(define ^:parent {Number} {abstract Float} [])

;; Covariant container (immutable, Flow-based)
(define {struct [^:covar T]} {Vector}
  [data {T}])

;; Invariant container (mutable, Slot-based)
(define {struct [^:invariant T]} {Array}
  [data {T} capacity {Int32}])

;; This means:
;; (Vector {Integer}) ⊑ (Vector {Number})  ✓ (covariant)
;; (Array {Integer}) ⊑ (Array {Number})  ✗ (invariant)

;; Union types
(define {Int32OrString}
  (union [{Int32} {String}]))
```

### Dispatch with Automatic Safety

```omnilisp
;; Compiler verifies: no ambiguity!
(define foo [x {(Vector {Integer})}] {String}
  "integer")

(define foo [x {(Vector {Float})}] {String}
  "float")

(define foo [x {(Vector {Number})}] {String}
  "number")

;; Dispatch is guaranteed unambiguous:
;; Call with (Vector {Integer} ...) → first method
;; Call with (Vector {Float} ...) → second method
;; Call with other Vector subtypes → third method
```

### Handling Ambiguity in OmniLisp

```omnilisp
;; Compiler detects ambiguity:
(define bar [x {(Vector {Integer})}] {Int32}
  1)

(define bar [x {(Vector {Float})}] {Int32}
  2)
;; ERROR: Ambiguous for (Vector {Number}) - no fallback

;; Solution 1: Add fallback method
(define bar [x {(Vector {Integer})}] {Int32}
  1)

(define bar [x {(Vector {Float})}] {Int32}
  2)

(define bar [x {(Vector {Number})}] {Int32}
  3)
;; OK!

;; Solution 2: Explicit priority (metadata)
;; Attach to one of the definitions:
(define ^:priority ((Vector {Integer}) (Vector {Float})) bar
  [x {(Vector {Integer})}] {Int32}
  1)

(define bar
  [x {(Vector {Float})}] {Int32}
  2)

;; Solution 3: Runtime resolution (metadata)
(define ^:ambiguous :runtime bar
  [x {(Vector {Integer})}] {Int32}
  1)

(define bar
  [x {(Vector {Float})}] {Int32}
  2)

;; Solution 4: Custom ambiguity handler (metadata)
(define bar
  [x {(Vector {Integer})}] {Int32}
  1)

(define ^:ambiguous (fn [x] {Int32} (error "Ambiguous dispatch for ~A" x))
  bar
  [x {(Vector {Float})}] {Int32}
  2)
```

---

## Trade-offs and Design Decisions

### What We Gain

| Feature | Benefit |
|---------|---------|
| Covariant containers | Intuitive subtyping, flexible APIs |
| Multiple dispatch | Composable polymorphism |
| Compile-time safety | Catch ambiguity early |
| Runtime flexibility | Support dynamic code |

### What We Accept

| Limitation | Mitigation |
|------------|------------|
| True ambiguity requires fallback | Compiler guides programmer |
| Can't detect all runtime errors | That's Lisp - programmer is responsible |
| Complex lattice interactions | Good tooling and error messages |

### Comparison with Alternatives

| Language | Dispatch | Containers | Ambiguity | Flexibility |
|----------|----------|------------|-----------|------------|
| Haskell | Type classes | Invariant | Compile-time error | Low (sound but rigid) |
| Java/C# | Single dispatch | Invariant | Compile-time error | Low |
| Common Lisp | Multiple dispatch | N/A | Runtime error | High |
| Julia | Multiple dispatch | Covariant | Runtime heuristic | High |
| **OmniLisp** | **Multiple dispatch** | **Covariant** | **Compile-time detect** | **High** |

---

## Implementation Phases

### Phase 1: Type Lattice Infrastructure
- [ ] Build type lattice from type definitions
- [ ] Implement subtype checking with lattice traversal
- [ ] Add join/meet operations for type combinations

### Phase 2: Method Analysis
- [ ] Parse method signatures
- [ ] Build applicability regions for each method
- [ ] Implement overlap detection algorithm

### Phase 3: Ambiguity Detection
- [ ] Pairwise method comparison
- [ ] Multi-parameter support
- [ ] Error reporting with helpful messages

### Phase 4: Resolution Strategies
- [ ] Require explicit fallback (strict mode)
- [ ] Support :priority annotations
- [ ] Generate runtime checks (flexible mode)
- [ ] Interactive disambiguation

### Phase 5: Tooling
- [ ] Visualize type lattice
- [ ] Explain ambiguity to user
- [ ] Suggest fixes
- [ ] Performance profiling

---

## Open Questions

### 1. How to handle Union types?

```omnilisp
;; Union types interact with dispatch:
(define foo [x {Int32}] {Int32}
  1)

(define foo [x {String}] {Int32}
  2)

;; What about (union [{Int32} {String}])?
;; Both applicable - which wins?
;; Solution: Add Union case or use ^:priority metadata
```

### 2. How to handle parametric type bounds?

```omnilisp
;; Bounded parametric types (using ^:where):
(define ^:where [T {Number}]
  {struct [^:covar T]} {NumericVector}
  [data {T}])

;; How does this interact with dispatch?
(define process [x {(NumericVector {Integer})}] {String}
  "integer vector")

(define process [x {(NumericVector {Number})}] {String}
  "number vector")
```

### 3. Gradual typing integration?

```omnilisp
;; How do static and dynamic types interact?
;; Untyped parameters (Slot without Kind):
(define baz [x {(Vector {Integer})}] {Int32}
  42)

(define baz [x] {Int32}  ; Dynamic (untyped)
  x)

;; Can we infer the second from the first?
;; Or do they coexist as separate methods?
```

### 4. Module system interactions?

```omnilisp
;; How does ambiguity work across modules?
;; Module A defines:
(define foo [x {(Vector {Integer})}] {Int32}
  1)

;; Module B defines:
(define foo [x {(Vector {Float})}] {Int32}
  2)

;; When both are imported - is this ambiguous?
;; Can we detect at module load time?
;; Solution: Module-level disambiguation metadata
```

---

## Conclusion

OmniLisp can achieve the **best of both worlds**:

1. **Flexibility of Lisp** - Runtime dynamism when needed
2. **Safety of static types** - Compile-time ambiguity detection
3. **Power of multiple dispatch** - Composable polymorphism via multiple `define`
4. **Intuition of covariance** - Natural subtyping for containers

The key insight: **Analyze what you can at compile time, fall back to runtime when you can't.**

This positions OmniLisp as:
- More flexible than Haskell (runtime dynamism)
- Safer than Julia (compile-time ambiguity detection)
- More powerful than Common Lisp (parametric types + multiple dispatch)
- More practical than Scheme (real-world type system)

**That's the sweet spot.** 🎯

---

## References

- Julia Type System: [A Type System for Julia](https://arxiv.org/abs/2009.07583)
- OmniLisp Syntax Revision: `docs/SYNTAX_REVISION.md`
- OmniLisp Language Reference: `docs/QUICK_REFERENCE.md`
- Common Lisp CLOS: Chapters on Multiple Dispatch
- Covariance and Subtyping: "On Variance and Subtyping"
- Type Lattice Algorithms: "Lattice-based Type Inference"

---

*Document created: 2025-01-08*
*Updated: 2026-01-08 - Aligned with OmniLisp Character Calculus syntax*
*For further discussion and refinement*
