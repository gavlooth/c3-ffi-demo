# Violet - A Clojure-like Language on Purple

## Overview

**Violet** is a Clojure-like language running on Purple via the tower of interpreters. It demonstrates how Purple's meta-programming capabilities enable creating entirely new languages through preprocessing and collapsing.

Violet provides:
- Full Clojure core compatibility (non-Java parts)
- Multimethods for polymorphic dispatch
- Protocols for type-based polymorphism
- Lazy sequences
- Threading macros (->, ->>, some->, cond->)
- Loop/recur for tail recursion
- List comprehensions (for, doseq)
- Optional persistent data structures via Immer FFI

**Key Principle**: Violet is implemented entirely through preprocessing - no modifications to Purple's core.

## Architecture

```
┌─────────────────────────────────────────────────────────────────┐
│                   lib/violet/core.violet                         │
│         Clojure functions adapted from clojure.core.clj          │
│   (take, drop, partition, group-by, frequencies, sort-by, ...)   │
└─────────────────────────────────────────────────────────────────┘
                              ↓ uses
┌─────────────────────────────────────────────────────────────────┐
│                         Purple Core                              │
│     (cons, car, cdr, map, filter, fold, +, -, *, /, ...)        │
└─────────────────────────────────────────────────────────────────┘

┌─────────────────────────────────────────────────────────────────┐
│                   lib/immer.purple (optional)                    │
│         Persistent vectors, maps, sets via Immer FFI            │
└─────────────────────────────────────────────────────────────────┘
                              ↓
┌─────────────────────────────────────────────────────────────────┐
│                 lib/immer/immer_bridge.cpp                       │
│                    C++ ↔ C Wrapper                               │
└─────────────────────────────────────────────────────────────────┘
```

## Design Principles

1. **Core Purple unchanged** - no new AST types, no parser changes
2. **Violet adapts clojure.core** - functions ported from official Clojure source
3. **FFI is optional** - `lib/immer.purple` for persistent data structures
4. **Minimal Purple, maximal Violet** - Purple provides primitives, Violet builds on them

## What's Implemented

### 1. C++ Bridge (`lib/immer/immer_bridge.cpp`)

Thin C wrapper around Immer C++ library:

```c
// Vector
void* immer_vector_empty(void);
void* immer_vector_push(void* vec, void* elem);
void* immer_vector_get(void* vec, int idx);
int   immer_vector_size(void* vec);
// ... etc

// Map
void* immer_map_empty(void);
void* immer_map_assoc(void* m, void* key, void* val);
void* immer_map_get(void* m, void* key, void* not_found);
// ... etc

// Set
void* immer_set_empty(void);
void* immer_set_conj(void* s, void* elem);
int   immer_set_contains(void* s, void* elem);
// ... etc
```

### 2. Purple FFI Wrapper (`lib/immer.purple`)

Uses Purple's existing FFI mechanism:

```scheme
;; Declare external functions
(ffi-declare "void*" "immer_vector_empty")
(ffi-declare "void*" "immer_vector_push" "void*" "void*")

;; High-level API
(define (vector . args)
  (reduce (lambda (v x) (ffi "immer_vector_push" v x))
          (ffi "immer_vector_empty")
          args))

(define (nth v idx)
  (ffi "immer_vector_get" v idx))

(define (hash-map . args) ...)
(define (get m key) ...)
(define (assoc m key val) ...)
;; ... etc
```

### 3. Clojure Core (`lib/clojure/core.purple`)

Pure Purple - no FFI:

```scheme
;; Higher-order functions
(define (map f coll)
  (if (null? coll)
      '()
      (cons (f (car coll)) (map f (cdr coll)))))

(define (filter pred coll) ...)
(define (reduce f init coll) ...)
(define (take n coll) ...)
(define (drop n coll) ...)
(define (partition n coll) ...)
(define (group-by f coll) ...)
(define (sort coll) ...)
;; ... 50+ functions
```

## Usage

```scheme
;; Load libraries
(load "lib/immer.purple")
(load "lib/clojure/core.purple")

;; Use persistent vectors
(define v (vector 1 2 3 4 5))
(define v2 (vector-push v 6))  ; v unchanged!

;; Use clojure-style functions
(map (lambda (x) (* x 2)) '(1 2 3))  ; => (2 4 6)
(filter even? '(1 2 3 4 5))          ; => (2 4)
(reduce + 0 '(1 2 3 4 5))            ; => 15
```

## Building

```bash
cd lib/immer

# Fetch Immer if needed
make fetch-immer

# Build bridge library
make

# Test
make test
```

## Linking with Purple Programs

When compiling Purple programs that use Immer:

```bash
# Compile Purple to C
./purple_go -c program.purple > program.c

# Compile and link
gcc -std=c99 -pthread program.c \
    -L./lib/immer -limmer_bridge \
    -lstdc++ -o program
```

## Memory Management

- Immer structures use structural sharing internally
- Purple's ASAP injects `immer_*_free()` calls at scope exit
- No garbage collection, fully deterministic

## Files

```
lib/
├── immer/
│   ├── immer_bridge.h      # C header
│   ├── immer_bridge.cpp    # C++ implementation
│   ├── Makefile            # Build system
│   └── test_bridge.cpp     # C++ tests
├── immer.purple            # FFI wrappers for persistent data structures
└── violet/
    ├── VIOLET.md           # Language documentation & directives
    ├── preprocess.purple   # Syntax transformer (Violet → Purple)
    ├── core.violet         # Core functions (~900 lines, adapted from clojure.core)
    ├── protocols.violet    # Protocol system implementation
    ├── multimethods.violet # Multimethod dispatch system
    ├── macros.violet       # Macro definitions & documentation
    ├── loader.purple       # Violet file loader
    ├── examples.violet     # Example programs
    └── test.violet         # Test suite
```

## Implementation Status

| Feature | Status | Notes |
|---------|--------|-------|
| Core functions | ✓ Complete | ~100 functions from clojure.core |
| Threading macros | ✓ Complete | ->, ->>, some->, cond->, as-> |
| Control flow | ✓ Complete | when, cond, if-let, when-let, case |
| Loop/recur | ✓ Complete | Transforms to recursive lambda |
| Multimethods | ✓ Complete | defmulti, defmethod, hierarchies |
| Protocols | ✓ Complete | defprotocol, extend-type, extend-protocol |
| Lazy sequences | ✓ Complete | Thunk-based implementation |
| Destructuring | ✓ Partial | Sequential only, map destructuring pending |
| Comprehensions | ✓ Complete | for, doseq, dotimes |
| Transducers | ✓ Partial | Basic implementation |
| Persistent data | ✓ Complete | Via Immer FFI |

## Future Work

- Reader macros for `[...]`, `{...}`, `#{...}` syntax sugar
- Map destructuring in let bindings
- Full transducer protocol
- spec-like validation
- core.async style channels (using Purple's channels)
