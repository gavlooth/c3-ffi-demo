# OmniLisp Language Reference

This is the authoritative reference for the OmniLisp language, its "Character Calculus" semantics, and its optionally-typed execution model.

---

## 1. The Character Calculus (Delimiters)

OmniLisp uses a fixed semantic "Charge" for every delimiter to ensure absolute consistency across the value and type domains.

| Character | Domain | Purpose |
| :--- | :--- | :--- |
| **`()`** | **Flow** | Execution logic, function calls, and **Value Constructors**. |
| **`[]`** | **Slot** | Argument vectors, `let` bindings, arrays, and **Type Parameters**. |
| **`{}`** | **Kind** | **Type Annotations** and the Blueprint domain. |
| **`.`**  | **Path** | Data location and navigation. Bridges Flow and Slot. |
| **`^`**  | **Tag**  | Metadata and out-of-band instructions. |
| **`#`**  | **Sign** | Reader dispatch for literal structures (Dicts). |

---

## 2. Bindings & Function Definitions

Functions use Slot `[]` syntax for parameters, with **optional** type annotations in Kind `{}`.

### 2.1 Function Definition Syntax

**Two forms are supported:**

#### Traditional Shorthand (Ergonomic Default)
```lisp
;; No types - concise for prototyping
(define add x y
  (+ x y))

;; Single parameter
(define square x
  (* x x))

;; Equivalent to:
(define add [x] [y]
  (+ x y))
```

#### Slot Syntax (Explicit/Typed)
```lisp
;; All parameters typed
(define add [x {Int}] [y {Int}] {Int}
  (+ x y))

;; Mixed - some typed, some not
(define map [fn] [xs {List}] {List}
  (fn xs))

;; All parameters in Slots, types optional
(define process [x] [y {Int}] [z {Float}]
  (body))
```

**Syntax Pattern:**
```lisp
(define function-name
  [param1 {Type}?]    ;; Slot with optional type
  param2              ;; Shorthand: param without Slot = untyped
  [param3 {Type}?]
  {ReturnType}?        ;; Optional return type
  body)
```

### 2.2 Diagonal Dispatch & Constraints (`^:where`)
To enforce that multiple arguments share the same type or to restrict generics.

```lisp
;; x and y MUST be the same T, where T is a Number
(define ^:where [T {Number}]
  [x {T}] [y {T}] {T}
  (+ x y))
```

### 2.3 Lambda Shorthands (`fn` and `λ`)
The symbols `fn` and `λ` use the same Slot pattern.

```lisp
;; Untyped parameters (shorthand)
(fn x y
  (+ x y))

;; Single parameter
(fn [x] (* x x))
(λ [x] (* x x))

;; Typed parameters
(fn [x {Int}] [y {Int}] {Int}
  (+ x y))

;; Mixed types
(fn [x] [y {String}] {String}
  (concat x y))
```

**Consistency Note:** Lambdas follow the same pattern as `define` - Slots `[]` for parameters, optional types in `{}`.

### 2.4 Local Bindings (`let`)
Let bindings use Slot `[]` syntax with optional types: `[name {Type}? value]`

```lisp
;; Untyped bindings (shorthand)
(let [x 10] [y 20]
  (+ x y))

;; Typed bindings
(let [x {Int} 10] [y {Int} 20]
  (+ x y))

;; Mixed - some typed, some not
(let [x 10] [y {Int} 20]
  (+ x y))

;; Sequential let (each binding sees previous ones)
(let ^:seq [x 1] [y (+ x 1)]
  y)
```

**Consistency:** Let bindings follow the same `[name {Type}?]` pattern as function parameters.

### 2.4.1 Destructuring in `let`

`let` supports minimal destructuring patterns for sequences and dictionaries/plists.

#### Sequence Destructuring
Positional destructuring works for **both lists and arrays**:

```lisp
;; Destructure a sequence into x, y, z
(let [[x y z] my-seq]
  (+ x y z))
;; my-seq can be: (list 1 2 3) OR [1 2 3] - same behavior!
```

#### Splicing
Capture remaining elements with `..`:

```lisp
;; Bind first two elements, capture rest
(let [[x y .. rest] my-seq]
  (append (list x y) rest))
;; x = first element, y = second, rest = remaining elements
```

#### Dictionary/Plist Destructuring
Pull symbols by name from dictionaries or property lists:

```lisp
;; Pull symbols :x and :y from dictionary/plist
(let [(:x :y) my-dict]
  (+ x y))
;; Equivalent to: x = my-dict[:x], y = my-dict[:y]
```

**Note:** In OmniLisp, `:x` is pure reader sugar for `'x` (no separate keyword type). The destructuring pattern `(:x :y)` extracts values associated with those symbols. For canonicalization/pretty-printing, prefer the `'x` form.

### 2.5 Type Enforcement
Every variable has an associated Kind. If no Kind is specified, it defaults to **`Any`**.
*   **Assignments:** `define`, `let`, and `set!` validate values against the Kind blueprint.
*   **Application:** `omni_apply` validates arguments against parameter Kinds and the result against the return Kind.

### 2.6 Multiple Dispatch & Multi-arity
OmniLisp functions support **Multiple Dispatch**, allowing a single name to refer to multiple implementations (methods). Dispatch is determined by:
1.  **Arity:** The number of arguments provided.
2.  **Specificity:** The Kind of each argument (Most specific match wins).

```lisp
;; 1. Multi-arity (different argument counts)
(define area 0)                      ; 0 args
(define area [r] (* 3.14 (* r r)))   ; 1 arg

;; 2. Type Specialization (Multiple Dispatch)
(define describe [x {Int}] "An integer")
(define describe [x {String}] "A string")

;; 3. Combined Dispatch
(define add [x {Int}] [y {Int}] (+ x y))
(define add [x {String}] [y {String}] (string-append x y))
```

---

## 3. The Type System (Kind Domain)

OmniLisp adopts a **Julia-compatible Type System** defined via the Uniform Definition syntax `(define {Kind ...} ...)`.

### 3.1 Type Definitions

#### Abstract Types
Abstract types cannot be instantiated. They serve as nodes in the type graph.

```lisp
;; Abstract type 'Number'
(define {abstract Number})

;; Abstract type with parent (using metadata)
(define ^:parent {Number} {abstract Integer} [])
```

#### Primitive Types
Types with a fixed bit-width representation, usually provided by the host/compiler.

```lisp
;; Define a 64-bit primitive type
(define ^:parent {Integer} {primitive Int64} [64])
```

#### Composite Types (Structs)
Product types that hold named fields. Immutable by default.

```lisp
;; Immutable Struct (with parent type)
(define ^:parent {Any} {struct Point}
  [x {Int}]
  [y {Int}])

;; Mutable Struct
(define {mutable-struct Person}
  [name {String}]
  [age  {Int}])
```

#### Union Types
Represents a value that can be one of several types. Unions are **Flow constructors** that return a Kind.

```lisp
;; Create a union type (Flow constructor)
(union [{Int} {String}])

;; Type alias: name the union
(define {IntOrString}
  (union [{Int} {String}]))

;; Empty union is the Bottom type (no values possible)
;; Equivalent to Julia's Union{}
(define {Bottom}
  (union []))  ; No values can have this type

;; Using union in annotations
(define process [val {(union [{Int} {String}])}]
  (match val
    [x {Int} (* x 2)]
    [s {String} (string-length s)]))
```

**Note:** The empty union `(union [])` is the **bottom type** (called `Union{}` in Julia). No value can have this type, making it useful for type theory and proving impossibility.

#### Parametric Types
Types that take type parameters (Generics).

```lisp
;; Parametric Struct
(define {struct Node [T]}
  [value {T}]
  [next  {Option {Node T}}])
```

### 3.2 Annotations vs. Constructors
*   **Annotation `{}`**: Used in definitions to restrict a slot.
*   **Constructor `()`**: Used at the value level to create instances.

```lisp
(define x {Int} 5)           ; {Int} is an annotation
(define p (Point 10 20))     ; (Point) is a constructor (Flow)
```

### 3.3 First-Class Kinds
Base types (`Int`, `String`, `Array`, `Any`, etc.) are physical Kind objects stored in the global environment. They can be manipulated as values.

```lisp
(define my-type Int)
(type? 10 my-type)           ; => true
```

### 3.4 Subtyping
OmniLisp uses a nominative subtype hierarchy. **`Any`** is the universal supertype.

Subtype relationships are declared using `^:parent` metadata:

```lisp
;; Int is a subtype of Number
(define ^:parent {Number} {abstract Int} [])

;; Number is a subtype of Any (implicit, as Any is root)
(define {abstract Number} [])
```

*   **`Any`** is the universal supertype (root of the type hierarchy)
*   **`Union{}`** (empty union) is the universal subtype (bottom type, has no values)
*   **`Nothing`** is a concrete singleton type with the single value `nothing` (similar to Julia)

### 3.5 Types as Values (First-Class Kinds)

In OmniLisp, types themselves are values that can be passed around, stored, and referenced. This leads to three distinct patterns in definitions:

#### Pattern 1: Type Inheritance (Metadata)
Use `^:parent` to declare that a type is a subtype of another:
```lisp
;; Integer IS-A Number (inheritance)
(define ^:parent {Number} {abstract Integer} [])
```

#### Pattern 2: Type Annotation (Kind)
Use `{}` to annotate that a value must conform to a type:
```lisp
;; my-number must be an Int (annotation)
(define my-number {Int} 42)

;; Function parameter with type constraint
(define process [x {String}] {Int}
  (string-length x))
```

#### Pattern 3: Type Objects as Values
A type object can be the **value** of a definition (not annotated, not metadata):
```lisp
;; IntType is a variable whose VALUE is the Int type object
(define IntType Int)

;; AnimalType holds the Animal type object
(define AnimalType Animal)

;; Using type objects for dispatch
(define check [value {Any} type-check {(Type {Int})}]
  (if (type? value type-check)
      "is an integer"
      "not an integer"))
```

**Key Distinction:**
- `^:parent {Parent}` = Metadata (declares inheritance)
- `{Type}` = Annotation (constrains a value)
- `Type` (no braces) = Value (the type object itself)

**Metadata Rule:** If the same metadata key appears multiple times (e.g. `^:parent` twice), the **last occurrence wins**.

### 3.6 Type Predicates

OmniLisp provides predicates for checking types at runtime.

#### `type?` Predicate
Check if a value is of a specific type:

```lisp
;; Check against Kind objects
(type? 42 Int)         ; => true
(type? "hello" Int)    ; => false
(type? "hello" String) ; => true
(type? [1 2 3] Array)  ; => true

;; Works with subtype relationships
(type? 5 Number)       ; => true (Int is a subtype of Number)
(type? 5 Any)          ; => true (everything is a subtype of Any)

;; Using type objects as values
(define IntType Int)
(type? 42 IntType)     ; => true
```

**Note:** The `type?` predicate uses the runtime type registry for subtype checking. Values of a subtype will return `true` when checked against their parent types.

## 4. Sequences & Iterators

### 4.1 Lazy Iterators
Core sequence functions are lazy and return `T_ITER` objects.
*   `(range n)`: Returns an iterator from 0 to n-1.
*   `(map f iter)`: Returns a lazy transformed iterator.
*   `(filter pred iter)`: Returns a lazy filtered iterator.

### 4.2 Realization
To convert a lazy iterator into an eager structure:
*   `(collect-list iter)`: Produces a Persistent Linked List `()`.
*   `(collect-array iter)`: Produces a Contiguous Realized Array `[]`.

---

## 5. Access & Mutation

### 5.1 Path Navigation
The dot `.` operator is used for nested data access.
*   `person.name` -> `(get person :name)`
*   `arr.[0]` -> `(get arr 0)`

### 5.2 Mutation
*   `set!`: Rebinds a name.
*   `put!`: Mutates a path (e.g., `(put! person.age 31)`).
*   `update`: Functionally transforms a path.

---

## 6. Control Flow & Effects

### 6.1 Algebraic Effects
OmniLisp uses **Algebraic Effects** as its primary mechanism for non-local control flow and error handling. Traditional `try/catch` is replaced by `handle/perform`.

*   **`handle`**: Establishes a handler for a specific set of effects.
*   **`perform`**: Triggers an effect, transferring control to the nearest handler.
*   **`resume`**: Resumes the suspended computation from within a handler.

```lisp
(define ^:one-shot {effect ask})

(handle
  (+ 1 (perform ask nothing))
  (ask [payload resume] (resume 42)))
;; => 43
```

### 6.2 Fibers & Channels (Two-Tier Concurrency)
OmniLisp implements a **Two-Tier Concurrency** model that separates physical parallelism from massive logical concurrency.

*   **Tier 1 (Parallel):** OS Threads (pthreads) for multi-core utilization.
*   **Tier 2 (Concurrent):** Lightweight **Fibers** (continuations) for massive concurrency (1M+ fibers).

#### Fiber Management
*   **`fiber`**: Creates a paused fiber from a thunk.
*   **`resume`**: Manually steps into a fiber (direct control).
*   **`yield`**: Suspends the current fiber, returning control to the caller or scheduler.
*   **`with-fibers`**: Establishes a local **Fiber Scheduler** scope. The block waits until all spawned fibers complete.
*   **`spawn`**: Registers a fiber with the current scheduler.
*   **`join`**: Blocks the current fiber until the target fiber completes, returning its result.
*   **`run-fibers`**: Explicitly runs the scheduler loop until all pending fibers are done.

#### Channels (CSP)
Fibers communicate via **Channels**, enabling ownership transfer without shared-memory locks.
*   **`chan`**: Creates an unbuffered (rendezvous) channel.
*   **`(chan n)`**: Creates a buffered channel with capacity `n`.
*   **`send` / `recv`**: Synchronous or buffered communication. If a channel is full (on send) or empty (on recv), the fiber **Parks** (Tier 2 suspension) to let others run.

```lisp
(with-fibers
  (define c (chan 3))
  (spawn (fiber (lambda [] (send c "ping"))))
```

#### Ownership Transfer
Sending a value through a channel performs an **Ownership Transfer**. The sending fiber/thread yields control of the object's lifetime to the receiver, preventing data races by ensuring only one owner exists at any time. Immutable objects (frozen) can be shared safely across Tier 1 threads.

---

## 7. Pika Grammar DSL

Define high-performance PEG grammars directly in the Flow domain.

```lisp
(define [grammar arithmetic]
  [expr (first (seq (ref term) "+" (ref expr)) (ref term))]
  [term (ref factor)]
  ...)
```

---

## 8. Memory Management (CTRR)

OmniLisp uses **CTRR (Compile-Time Region Reclamation)**: a deterministic,
garbage-collection-free model where the compiler schedules region lifetimes and
inserts explicit operations for escapes and borrows.

- **Regions:** Most allocations are bump/arena allocations in a region.
- **Transmigration (escapes):** Values that cross a region boundary are repaired
  by moving/copying their object graph into an outliving region.
- **Tethering (borrows):** Borrow windows pin regions so they cannot be reclaimed
  while in use.

Canonical references:

- `docs/CTRR.md` (short, normative spec)
- `runtime/docs/CTRR_TRANSMIGRATION.md` (detailed transmigration contract)

---

## 9. Deprecated Namespaces

The following are legacy and should not be used:
*   `violet.*`: Use core primitives instead.
*   `scicomp.*`: Pending modern refactor.

---

## Appendix A: Type Definition Reference

The `(define ...)` form unifies all top-level definitions.

### Abstract Types
```lisp
(define {abstract Animal} [])
(define ^:parent {Animal} {abstract Mammal} [])
```

### Concrete Structs (Immutable Default)
```lisp
(define {struct Point}
  [x {Float}]
  [y {Float}])

;; With Parent Type (Metadata Syntax)
(define ^:parent {Shape} {struct Circle}
  [center {Point}]
  [radius {Float}])
```

### Parametric Types
```lisp
(define {struct [Pair T]}
  [first {T}]
  [second {T}])

;; Parametric with Parent
(define ^:parent {Any} {struct [Entry K V]}
  [key {K}]
  [value {V}])
```

### Mutable Structs
```lisp
(define {struct Player}
  [^:mutable hp {Int}]  ; Field-level mutability
  [name {String}])

;; Whole struct mutable sugar
(define ^:mutable {struct Player} ...)
```

### Enums (Sum Types)
```lisp
(define {enum Color} Red Green Blue)

(define {enum Option T}
  (Some [value {T}])
  None)
```

---

## Appendix B: Function Parameter Forms

| Syntax | Meaning |
|--------|---------|
| `x` | Untyped parameter |
| `[x {Int}]` | Typed parameter |
| `[x 10]` | Parameter with default value |
| `[x {Int} 10]` | Typed parameter with default |


## Tower of interpeters
```lisp
(lift val)           ; value → code
(run code)           ; execute code (JIT)
(EM expr)            ; meta-level evaluation
(shift n expr)       ; go up n levels
(meta-level)         ; current tower level
(clambda (x) body)   ; compile lambda

```

## Lexical Rules

### Symbol Character Rules

Symbols can contain letters and certain operators, but have specific rules about which characters can appear where.

**Start with (first character):**
- Letters: `a-z`, `A-Z`
- Operators: `*`, `!`, `-`, `_`, `?`, `%`, `/`, `=`, `<`, `>`

**Excluded from start:**
- Digits: `0-9` (to avoid confusion with integers)
- Reserved syntax: `.`, `@`, `#`, `&`, `:`, `;`

**Middle/subsequent characters:**
- All of the above (letters + operators)
- **Plus:** Digits `0-9`

**Excluded entirely:**
- `.` - Used for module paths (`Math.sin`)
- `@` - Used for metadata (`^:where`)
- `#` - Used for reader macros (`#val`, `#\newline`)
- `&` - Excluded
- `:` - Used for type annotations (`{Type}`)
- `;` - Used for comments

**Convention (not enforced):**
`!` and `?` are typically only at the **start or end** of symbols:
- At end: `set!`, `define!`, `null?`, `empty?`
- At start: `!not`, `!null`, `?maybe`, `?value`
- Not in middle: `foo!bar`, `set!value` (conventionally weird)

**Examples:**
```scheme
; Valid symbols
foo                ; letters
foo-bar            ; - as separator
foo123             ; digits in middle
x1_y2              ; _ as separator
set!               ; ! at end
null?              ; ? at end
!not               ; ! at start
?maybe             ; ? at start
*                  ; single operator
+                  ; single operator
<=                 ; comparison operators
==                 ; equality
50%off             ; % in middle
3/4                ; / in middle

; Invalid (can't start with digits)
123foo             ; integer, not symbol
3d                 ; digit first
7up                ; digit first

; Invalid (reserved for syntax)
.foo               ; . for paths
foo.bar            ; . for paths (not a single symbol)
@meta              ; @ for metadata
#reader           ; # for reader macros
&and               ; & excluded
:type              ; : for types
comment;more       ; ; for comments
```

## Data Types
```scheme
42              ; integer
3.14            ; float
'foo            ; symbol
#\a #\newline   ; character
'(1 2 3)        ; list
()              ; empty list (truthy)
nothing         ; nothing (falsy)
false           ; false (falsy)
true            ; true
0               ; integer (truthy)
```

## Special Forms
**Implemented subset (current C compiler/runtime):** `define`, `lambda`/`fn`, `let`, `let*`, `if`, `do`/`begin`, and `quote`. Other forms listed below are design target.
```scheme
; Let bindings with destructuring
(let [x 10] [y 20]                    ; Simple bindings
  (+ x y))

(let [[x y z] my-vec]                 ; Array destructuring
  (+ x y z))

(let [(Point x y) my-point]            ; Constructor destructuring
  (* x y))

(let [x {Int} 10] [y {Int} 20]        ; Typed bindings
  (+ x y))

(let ^:seq [x 1] [y (+ x 1)]         ; Sequential (like let*)
  (* x y))

; Traditional list-style also supported
(let ((x 10) (y 20))
  (+ x y))

; Lambda/function
(lambda (x) body)                    ; Function
(lambda self (x) body)               ; Recursive function
(fn x y (* x y))                     ; Shorthand (without parens)

; Conditionals and control flow
(if cond then else)                  ; Conditional
(and e1 e2 ...)                      ; Short-circuit and
(or e1 e2 ...)                       ; Short-circuit or
(do e1 e2 ... en)                    ; Sequence, return last

; Quoting
(quote x) / 'x                       ; Quote
(quasiquote x) / `x                  ; Quasiquote
(unquote x) / ,x                     ; Unquote in quasiquote
(unquote-splicing x) / ,@x           ; Splice in quasiquote
```
