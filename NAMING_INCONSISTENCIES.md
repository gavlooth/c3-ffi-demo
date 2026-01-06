# OmniLisp Syntax Ambiguity & Consistency Report (Final Authorization)

## 1. Problem Statement
To ensure total consistency across documentation and implementation, OmniLisp adopts a strict functional mapping for all delimiters. This eliminates ambiguity between value-level code, the type system, and the metadata system.

## 2. Definitive Character Roles

| Character | Domain | Role | Specific Uses |
| :--- | :--- | :--- | :--- |
| **`()`** | **Execution** | **Logic & Links** | Function calls, special forms, persistent Linked Lists. |
| **`[]`** | **Definition** | **Slots & Blocks** | Argument vectors, binding pairs, contiguous Arrays. |
| **`{}`** | **Types** | **Kinds & Structure** | Type annotations, type constructors, type headers. |
| **`^`**  | **Metadata** | **Hints & Tags** | Visibility, compiler hints, docstrings. |

---

## 3. Implementation Rules

### Rule 1: Flat Match Forms
`match` branches use an even number of forms. Grouping with `[]` is optional but encouraged for complex patterns.
*   *Correct*: `(match x (Some v) v None default)`

### Rule 2: The Argument Vector
Function arguments use `[]`. This distinguishes the "binding site" from the function logic.
*   *Correct*: `(define add [x y] (+ x y))`

### Rule 3: Iterators as Type Objects
Sequence operations (`map`, `filter`, `range`) are functions that return instances of the `{Iter T}` type.
*   *Lazy Default*: `(map f xs)` returns a transient `{Iter T}` object.
*   *Materialization*: To realize an iterator into storage, wrap it in a constructor: `(list (range 5))` or `(array (range 5))`.

### Rule 4: Type Braces `{}`
Curly braces are reserved strictly for **Types and Type Annotations**.
*   *Annotation*: `[x {Int}]`
*   *Type Construction*: `{Option {List String}}`
*   *Generic Header*: `{struct Pair [T]}`

### Rule 5: Metadata Prefix `^`
The caret is reserved strictly for **Metadata and Hints**. This information is typically "extra-logical"—it doesn't change the type of the value, but changes how the compiler or environment treats the binding.
*   *Visibility*: `^:private`
*   *Optimization*: `^:hot`, `^:inline`
*   *Documentation*: `^"Calculates the sum"`

---

## 4. Syntax Examples (Final Spec)

### 4.1 Type Annotation vs Metadata
```lisp
;; {Float} is the Type (Kind).
;; ^:private is the Metadata (Hint).
(define ^:private [version {Float} 1.0])
```

### 4.2 Function with Type Signatures
```lisp
(define process [xs {Iter Int}] {Iter Int}
  (filter (-> n (> n 10)) 
    (map (-> n (* n 2)) xs)))
```

---

## 6. Agreed Syntax Evolution (v0.7.0+)

### 6.1 String Formatting & Interpolation
OmniLisp uses reader tags to opt-in to complex string logic, keeping standard `"` literals pure.
*   **`#fmt"..."`**: Modern interpolation using `{expression}` or `{}` placeholders.
*   **`#clf"..."`**: Traditional `cl-format` (Common Lisp style) using `~` directives.

### 6.2 Data Layout & Concurrency Hints
Metadata hints guide the compiler's C99 code generation without leaking low-level memory management to the logic.
*   **`^:inline`**: Informs the compiler to embed a child structure directly into the parent (flat layout) rather than using a pointer.
*   **`^:atomic`**: Informs the compiler that a field or binding must be accessed using atomic primitives (for thread-safe mutation).

### 6.3 Mutation & Transformation (`set!`, `put!`, `update`)
OmniLisp distinguishes between modifying a **Name** (Binding) and modifying a **Structure** (Slot).

*   **`set!` (Binding Domain)**: Modifies a local or global variable. 
    *   *Returns*: The **assigned value**.
    *   *Usage*: `(set! x 10)`.
*   **`put!` (Slot Domain)**: Modifies a location within a structure identified by a **Path**. 
    *   *Returns*: The **root object** of the path (fluent pass-through).
    *   *Usage*: `(put! person.age 30)` or `(put! data.items.(idx) val)`.
*   **`update!` (In-place Transformation)**: Applies a function to a slot.
    *   *Returns*: The **root object**.
    *   *Usage*: `(update! player.hp dec)`.
*   **`update` (Functional Transformation)**: Returns a new object with the transformation applied.
    *   *Returns*: A **new version** of the root object (with structural sharing or ASAP in-place optimization).
    *   *Usage*: `(define p2 (update p1.x inc))`.

### 6.4 The Dot Operator as Path Locator
The dot `.` is an **Access Operator** that defines a path to a slot. When used as the target of `put!` or `update`, it acts as a **Locator** rather than a value-fetcher.
*   **Static Path**: `obj.field.subfield`
*   **Dynamic Path**: `obj.(computed-index)`
*   **Symbol/String Path**: `obj.(:key)` or `obj.("string-key")`

---

## 7. Summary of Character Roles
*   **`[]`** : **Slot**. Defines a place for a value (Params, Arrays, Bindings).
*   **`{}`** : **Kind**. Defines the nature of the value (Types, Annotations).
*   **`()`** : **Flow**. Defines the execution of logic (Calls, Lists, Patterns).
*   **`.`**  : **Path**. Bridges Flow and Slot (Access and Locating).
*   **`^`**  : **Tag**. Annotates with side-information (Hints).
*   **`#`**  : **Sign**. Reader-level interpretations (Literals, Formats).

---

## 8. Architectural Intent
1.  **Code** `()`: What the program **does**.
2.  **Bindings** `[]`: Where the program **stores** it.
3.  **Types** `{}`: What the data **is**.
4.  **Metadata** `^`: How the program **behaves** (hints).
5.  **Paths** `.` : How the program **reaches** data.
6.  **Reader Tags** `#`: How the program **interprets** literals.
