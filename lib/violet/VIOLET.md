# Violet - A Clojure-like Language via Tower of Interpreters

## Philosophy

**Violet is not Purple.** Violet is a separate language that demonstrates the power of Purple's tower of interpreters architecture. The point of having collapsing interpreters is to create *different languages* that transform down to Purple primitives.

```
┌─────────────────────────────────────────────────────────────────┐
│                         Violet                                   │
│   Clojure-like syntax, multimethods, protocols, destructuring   │
└─────────────────────────────────────────────────────────────────┘
                              │ preprocessing
                              ▼
┌─────────────────────────────────────────────────────────────────┐
│                         Purple                                   │
│        Minimal Lisp: cons, car, cdr, lambda, if, define         │
└─────────────────────────────────────────────────────────────────┘
                              │ evaluation
                              ▼
┌─────────────────────────────────────────────────────────────────┐
│                      Native Code / JIT                           │
└─────────────────────────────────────────────────────────────────┘
```

## The Tower of Interpreters Approach

Purple provides a minimal core with meta-programming capabilities. Violet uses these to build a Clojure-compatible language:

1. **Preprocessing** - Violet source is transformed to Purple before evaluation
2. **Macros** - Complex forms expand to simple Purple primitives
3. **Runtime Libraries** - Protocols, multimethods implemented in Purple itself
4. **Collapsing** - All Violet constructs collapse to Purple, which collapses to native code

This is the power of the tower: each level can define its own semantics, and they compose cleanly.

## What Violet Provides (Clojure Compatibility)

### Supported Features

| Feature | Status | Implementation |
|---------|--------|----------------|
| Variadic functions `(fn [& args])` | ✓ | Preprocessing |
| Destructuring `(let [[a b] v])` | ✓ | Preprocessing |
| Multimethods | ✓ | Runtime library |
| Protocols | ✓ | Runtime library |
| Threading macros `->`, `->>` | ✓ | Macro expansion |
| `when`, `when-not`, `when-let` | ✓ | Macro expansion |
| `cond`, `condp`, `case` | ✓ | Macro expansion |
| `loop`/`recur` | ✓ | Tail-call transformation |
| Lazy sequences | ✓ | Thunks + memoization |
| Atoms, refs | ✓ | Purple primitives |
| Core functions | ✓ | Pure Violet/Purple |

### Explicitly NOT Supported (Java-specific)

| Feature | Reason |
|---------|--------|
| `proxy` | Java class generation |
| `reify` | Java interface implementation |
| `gen-class` | Java bytecode generation |
| Java interop (`.method`, `Class/static`) | No JVM |
| `import` | Java packages |

## File Structure

```
lib/violet/
├── VIOLET.md           # This document
├── preprocess.purple   # Syntax transformer (Violet → Purple)
├── core.violet         # Core functions (adapted from clojure.core)
├── protocols.violet    # Protocol system
├── multimethods.violet # Multimethod dispatch
├── macros.violet       # Core macro definitions
├── lazy.violet         # Lazy sequence implementation
└── test.violet         # Test suite
```

## How Preprocessing Works

Violet source files (`.violet`) are preprocessed before Purple evaluation:

```scheme
;; Violet input
(defn greet [name & titles]
  (str "Hello, " (apply str (interpose " " titles)) " " name))

;; After preprocessing (Purple)
(define greet
  (lambda (name titles)  ; titles is the rest-arg list
    (str "Hello, " (apply str (interpose " " titles)) " " name)))

;; Call site transformation
(greet "Alice" "Dr." "Prof.")
;; becomes
(greet "Alice" (list "Dr." "Prof."))
```

### Preprocessing Phases

1. **Variadic Transform** - `[a & rest]` → fixed arity + list packing
2. **Destructuring Transform** - `[[a b] c]` → nested `let` bindings
3. **Macro Expansion** - `->`, `cond`, `when`, etc.
4. **Syntax Normalization** - `[]` → `(vector)`, `{}` → `(hash-map)`

## Multimethods

Multimethods provide polymorphic dispatch based on arbitrary dispatch functions:

```scheme
;; Define a multimethod with dispatch function
(defmulti area :shape)

;; Define methods for different dispatch values
(defmethod area :circle [shape]
  (* 3.14159 (:radius shape) (:radius shape)))

(defmethod area :rectangle [shape]
  (* (:width shape) (:height shape)))

(defmethod area :default [shape]
  (error "Unknown shape"))

;; Usage
(area {:shape :circle :radius 5})      ; => 78.54
(area {:shape :rectangle :w 3 :h 4})   ; => 12
```

### Implementation

Multimethods are implemented as closures over dispatch tables:

```scheme
;; defmulti creates:
;; 1. A dispatch function
;; 2. A mutable method table (atom)
;; 3. A dispatch function that looks up and calls the right method
```

## Protocols

Protocols define a set of functions that can be implemented for different types:

```scheme
;; Define a protocol
(defprotocol ISeq
  (first [coll])
  (rest [coll])
  (cons [coll x]))

;; Extend to a type
(extend-type List
  ISeq
  (first [coll] (car coll))
  (rest [coll] (cdr coll))
  (cons [coll x] (cons x coll)))

;; Extend to another type
(extend-type Vector
  ISeq
  (first [coll] (vector-get coll 0))
  (rest [coll] (vector-drop coll 1))
  (cons [coll x] (vector-cons x coll)))
```

### Implementation

Protocols use type-based dispatch tables:

```scheme
;; Each protocol function checks the type of its first argument
;; and dispatches to the appropriate implementation
```

## Running Violet

```bash
# Run a Violet file
./purple_go --lang violet program.violet

# Or use the Violet loader
./purple_go -e '(load-violet "program.violet")'

# REPL with Violet semantics
./purple_go --lang violet --repl
```

## Design Principles

1. **Purple stays minimal** - No changes to Purple for Violet features
2. **Everything collapses** - All Violet constructs reduce to Purple
3. **Preprocessing over interpretation** - Transform once, run fast
4. **Clojure semantics, not syntax** - Focus on behavior, not Java interop
5. **Composition** - Violet features compose because Purple composes

## CRITICAL DIRECTIVE

**DO NOT MODIFY THE PURPLE CORE LANGUAGE** to add Violet features.

All Violet functionality MUST be implemented through:
- Preprocessing (syntax transformation)
- Macros (compile-time expansion)
- Runtime libraries (pure Purple code)
- The tower of interpreters mechanism

The ONLY exception is if explicitly requested by the project maintainer.

This is the entire point of the tower of interpreters architecture: new languages are built ON TOP of Purple, not by changing Purple. If you find yourself wanting to modify `pkg/eval/eval.go`, `pkg/parser/pika.go`, or other core files to support Violet - STOP. Find a way to do it through preprocessing or the language itself.

## Extending Violet

To add new Violet features:

1. Define the transformation in `preprocess.purple`
2. Add runtime support in appropriate library file
3. Add macros in `macros.violet` if needed
4. Test in `test.violet`

The tower of interpreters means you can even create languages on TOP of Violet - the pattern continues upward.
