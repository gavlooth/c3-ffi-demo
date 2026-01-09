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

**Note:** In OmniLisp, `:x` creates a symbol (equivalent to `'x`), not a keyword. The destructuring pattern `(:x :y)` extracts the values associated with those symbols from the dictionary.

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

### 6.3 Pattern Matching (`match`)

**Pattern matching is the source of truth for all control flow in OmniLisp.** The `if` form is syntactic sugar that desugars to a binary `match`.

#### Syntax

```lisp
(match expr
  pattern1 result1
  pattern2 result2
  ...
  _ default)           ; Wildcard pattern (catch-all)
```

**Key Design Decisions:**
- **Implicit pairs:** Each `(pattern result)` pair forms a match clause
- **`_` wildcard:** Catches all unmatched patterns (should be last)
- **Flat syntax:** No grouping needed - patterns and results alternate
- **Exhaustiveness:** Compiler may warn if non-exhaustive (future feature)

#### Pattern Types

##### 1. Literal Patterns
Match exact values:

```lisp
(match x
  1 "one"
  2 "two"
  true "yes"
  false "no"
  _ "other")
```

##### 2. Variable Patterns
Bind any value to a name:

```lisp
(match value
  x (println "Got: " x))  ; x binds to value
```

##### 3. Destructuring Patterns
Decompose sequences and structures:

```lisp
;; Array/list destructuring
(match [1 2 3]
  [x y z] (+ x y z))      ; x=1, y=2, z=3

;; Nested destructuring
(match (list 1 (list 2 3))
  [1 [x y]] (+ x y))      ; x=2, y=3

;; Dictionary destructuring
(match #{:name "Alice" :age 30}
  #{:name n :age a} (println n " is " a " years old"))
```

##### 4. Splicing Patterns
Capture remaining elements with `..`:

```lisp
(match my-list
  [x y .. rest] (cons x (cons y rest))  ; Bind first two, capture rest
  _ nil)
```

##### 5. Type Constraints
Patterns can include type annotations using `{}`:

```lisp
(match value
  [n {Int}] (* n 2)         ; Only matches if n is an Int
  [s {String}] (string-length s)
  _ 0)
```

##### 6. Guards (`when`)
Add additional conditions to patterns:

```lisp
(match x
  [n {Int}] (when (> n 10)) "large"
  [n {Int}] (when (< n 0)) "negative"
  [n {Int}] "small or zero"
  _ "not an integer")
```

**Guard Semantics:**
- Guards are evaluated **after** the pattern matches
- If the guard is false, matching continues to the next clause
- Guards can reference bindings from the pattern

#### `if` as Derived Form

The `if` special form desugars to a binary match:

```lisp
;; Source
(if condition then-branch else-branch)

;; Desugars to
(match condition
  true then-branch
  false else-branch)
```

**Why this matters:**
- Single optimization pass for all control flow
- Match compiler can emit branchless code for binary boolean matches
- Consistent semantics across all branching constructs

#### Pattern Matching and Perceus Reuse

Pattern matching enables **Perceus-style in-place updates**:

```lisp
;; Can reuse x's memory for the result
(match x
  [(Some y) (consume y)]    ; consume() indicates x is no longer needed
  [(None) (default-value)])
```

When the compiler proves `x` is dead after the match, it can reuse `x`'s memory for the result (same-size optimization).

#### Examples

```lisp
;; Factorial with pattern matching
(define factorial [n {Int}] {Int}
  (match n
    0 1
    1 1
    _ (* n (factorial (- n 1)))))

;; List processing with guards
(define sum-positive [nums {List}] {Int}
  (match nums
    _ (when (empty? nums)) 0
    [head .. tail] (when (> head 0)) (+ head (sum-positive tail))
    [head .. tail] (sum-positive tail)))

;; Type-safe option handling
(define get-or-default [opt {(Option {Int})}] [default {Int}] {Int}
  (match opt
    [(Some value)] value
    [(None)] default))
```

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

## 8. Memory Management (ASAP & RC-G)

OmniLisp utilizes a unique, garbage-collection-free memory model designed for predictability and performance.

### 8.1 ASAP (As Static As Possible)
The compiler statically analyzes variable lifetimes and automatically injects deallocation calls. This minimizes runtime overhead and eliminates "stop-the-world" pauses.

### 8.2 RC-G (Region-Based Reference Counting)
For dynamic data, OmniLisp uses **Region-Based Reference Counting**.
*   **Regions**: Objects are allocated into regions (arenas) that track their own internal connectivity.
*   **Transmigration**: The system can move objects between regions to optimize locality and clear cycles.
*   **Tethers**: Thread-local references that prevent premature deallocation while a value is in active use.

#### Phase 24 Performance Optimizations (2026-01-08)

The RC-G model has been heavily optimized with 9 major improvements achieving 2.7x-21.1x speedups:

| Optimization | Benefit |
|--------------|---------|
| Inline allocation buffer | 6.99x faster for small objects (< 64 bytes) |
| Specialized constructors | 5.55-6.32x faster batch list/tree allocation |
| Bitmap-based cycle detection | 2.7-12.5x faster transmigration |
| Region splicing | O(1) result-only region transfer (1.4-1.9x faster) |
| Region pooling | 21.1x faster small region creation |
| Inline fastpaths | Zero call overhead for hot operations |

**Key implementation details:**
- Regions use a 512-byte inline buffer for small object allocation
- Bitmap-based cycle detection replaces hash tables for O(1) visited tracking
- Thread-local region pool (32 regions per thread) eliminates malloc overhead
- Region splicing provides O(1) arena chunk transfer for functional patterns

See `runtime/bench/BENCHMARK_RESULTS.md` for detailed performance data.

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
()              ; empty list
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
(if cond then else)                  ; Conditional (desugars to match)
(and e1 e2 ...)                      ; Short-circuit and
(or e1 e2 ...)                       ; Short-circuit or
(do e1 e2 ... en)                    ; Sequence, return last

; Pattern matching (match is the source of truth)
(match expr
  pattern1 result1
  pattern2 result2
  ...
  _ default)                         ; Wildcard fallback

;; Match examples:
;; Simple value matching
(match x
  1 "one"
  2 "two"
  _ "other")

;; Destructuring patterns
(match [1 2 3]
  [x y z] (+ x y z)                  ; Bind x=1, y=2, z=3
  _ 0)

;; Guards (when clauses)
(match x
  [n {Int}] (when (> n 10)) "large"  ; Type constraint + guard
  [n {Int}] "small"
  _ "not an int")

;; Nested destructuring
(match (list 1 (list 2 3))
  [1 [x y]] (+ x y)                  ; x=2, y=3
  _ 0)

;; Splicing patterns
(match my-list
  [x y .. rest] (cons x (cons y rest))  ; Bind first two, capture rest
  _ nil)

; Quoting
(quote x) / 'x                       ; Quote
(quasiquote x) / `x                  ; Quasiquote
(unquote x) / ,x                     ; Unquote in quasiquote
(unquote-splicing x) / ,@x           ; Splice in quasiquote
```
