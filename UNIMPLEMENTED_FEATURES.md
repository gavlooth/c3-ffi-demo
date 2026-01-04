# OmniLisp Unimplemented Features

This document tracks features that are documented in the language design but not yet implemented in the runtime/compiler.

---

## Critical Gaps (Blocking Production Use)

### 1. Strings & Characters

**Status:** No T_STRING or T_CHAR types exist

**Missing:**
- String literals: `"hello world"`
- Character literals: `#\a`, `#\newline`
- String operations: `string-length`, `string-append`, `substring`, `string-ref`
- String interpolation

**Impact:** Blocks all real I/O, text processing, user interaction

**Location:** `omnilisp/src/runtime/types.h` needs T_STRING, T_CHAR tags

---

### 2. Float Type & Numeric Operations

**Status:** No T_FLOAT tag; floats treated as ints

**Missing:**
- Float literals: `3.14`, `1.0e-5`
- Transcendental math: `sin`, `cos`, `exp`, `log`, `sqrt`
- Hex/binary literals: `0xFF`, `0b1010`

**Evidence:**
- `csrc/codegen/codegen.c:808` - "treat floats as ints (TODO: proper float support)"
- `omnilisp/src/runtime/reader/omni_reader.c:433` - "TODO: Add T_FLOAT to types.h"

**Impact:** Blocks scientific computing (BLAS/Torch integration)

---

### 3. Array & Dict Access/Mutation

**Status:** Creation works, but access/mutation not implemented

**What works:**
```scheme
(array 1 2 3)        ; creates array
(dict :a 1 :b 2)     ; creates dict
```

**Missing:**
```scheme
arr.(0)              ; array indexing
(set! arr.(0) value) ; array mutation
arr.[1:4]            ; array slicing
arr.[::2]            ; stride slicing

dict.:key            ; dict access
(keys dict)          ; get all keys
(values dict)        ; get all values
```

**Evidence:** `omnilisp/src/runtime/eval/omni_eval.c:812` - "TODO: Handle field access (obj.field) and index access (arr.(i))"

**Impact:** Can't work with real data structures

---

### 4. Type System Features

**Status:** Basic `type` form exists; no struct/enum/interface

**Missing:**
```scheme
; Struct definitions
(define {struct Point}
  [x int]
  [y int])

; Enum definitions
(define {enum Color}
  Red Green Blue)

; Parametric types
{struct [Pair T]}

; Type annotations
(define (add [x int] [y int]) -> int
  (+ x y))

; Interfaces
(define {interface Printable}
  [print (-> self String)])
```

**Impact:** Can't define domain types; blocks organized code

---

### 5. FFI & External Handles

**Status:** Not implemented

**Missing:**
```scheme
(extern printf)                    ; declare external function
(opaque Handle :destructor free)   ; opaque C types
(@ffi "libm.so" "sin" double)      ; low-level FFI
```

**Impact:** Can't integrate with C libraries (BLAS, Torch, system calls)

---

### 6. File I/O

**Status:** Not implemented

**Missing:**
```scheme
(open "file.txt" :read)
(read-line port)
(write port "data")
(close port)
(with-open-file [f "file.txt"] ...)
```

**Impact:** Can't read/write files

---

## Partial Implementation (Forms Exist But Incomplete)

### Modules

**Status:** `module`, `import`, `export` forms parse but no real isolation

```scheme
(module my-module
  (export foo bar))    ; parses but doesn't enforce visibility

(import my-module)     ; parses but doesn't actually load
```

**What's needed:** Module loading, namespace isolation, visibility enforcement

---

### Concurrency

**Status:** T_CHAN and T_PROCESS types exist in runtime but no evaluator support

```scheme
(spawn (lambda () ...))  ; not wired
(channel)                ; not wired
(send ch val)            ; not wired
(recv ch)                ; not wired
```

**What's needed:** Wire up primitives in `omni_env_init()`

---

### Metadata

**Status:** Parser recognizes metadata syntax but not attached to values

```scheme
^:private              ; parsed but ignored
^:deprecated           ; parsed but ignored
^"docstring"           ; parsed but ignored
```

**Evidence:** `omnilisp/src/runtime/eval/omni_eval.c:545` - "TODO: attach metadata"

---

## Not Started (Design Only)

### Destructuring in Bindings

```scheme
(let [[x y] [1 2]] ...)           ; not implemented
(define [a b c] [1 2 3])          ; not implemented
(let [(cons h t) my-list] ...)    ; not implemented
```

---

### Advanced Function Features

```scheme
; Default parameters
(define (greet [name "Guest"]) ...)

; Named/keyword arguments
(define (make-point & :x x :y y) ...)

; Variadic functions
(define (sum .. nums) (reduce + 0 nums))

; Partial application
(partial add 5)

; Anonymous shorthand
#(+ % 1)    ; same as (lambda (x) (+ x 1))
```

---

### Reader Macros

```scheme
#(+ % 1)         ; anonymous function shorthand
#?(:clj x :cljs y)  ; conditional compilation
#_expr           ; discard form
#|block comment|#
#uuid"..."       ; typed literals
#path"..."
```

---

### Keywords

```scheme
:keyword         ; should be shorthand for 'keyword
:foo :bar        ; self-evaluating symbols
```

---

### Pipe Operators

```scheme
(|> value
    (f arg)
    (g arg))     ; threading macro

(try expr
  (catch e handler)
  (finally cleanup))
```

---

### Specialized Collections

```scheme
(tuple 1 2 "three")              ; fixed-size heterogeneous
(named-tuple [x 1] [y 2])        ; named fields
(Some value)                      ; option type
None
(Ok value)                        ; result type
(Err error)
```

---

### Generators/Iterators

```scheme
(define (gen-range n)
  (generator
    (for [i (range n)]
      (yield i))))
```

---

## Source Code TODOs

| File | Line | Issue |
|------|------|-------|
| `omnilisp/src/runtime/eval/omni_eval.c` | 812 | Handle field access (obj.field) and index access (arr.(i)) |
| `omnilisp/src/runtime/eval/omni_eval.c` | 545 | Attach metadata |
| `omnilisp/src/runtime/reader/omni_reader.c` | 433 | Add T_FLOAT to types.h |
| `csrc/codegen/codegen.c` | 808 | Proper float support |
| `csrc/codegen/codegen.c` | 1260 | Array literals |
| `omnilisp/src/runtime/memory/scc.c` | 389 | Sophisticated freeze point detection |

---

## What IS Working

### Special Forms (18)
- Control flow: `if`, `do`/`begin`, `when`, `unless`, `cond`
- Bindings: `let`, `define`, `set!`
- Functions: `lambda`, `fn`
- Patterns: `match`
- Collections: `array`, `dict` (creation only)
- Iteration: `for`, `foreach`
- Delimited continuations: `prompt`, `control`
- Algebraic effects: `defeffect`, `handle`, `perform`, `resume`
- Conditions/restarts: `handler-case`, `handler-bind`, `restart-case`, `with-restarts`, `call-restart`
- Debugging: `call-stack`, `stack-trace`, `effect-trace`, `effect-stack`

### Primitives (35+)
- Arithmetic: `+`, `-`, `*`, `/`, `%`
- Comparison: `<`, `>`, `<=`, `>=`, `=`
- Lists: `cons`, `car`, `cdr`, `null?`, `list`, `length`
- Higher-order: `map`, `filter`, `reduce`, `range`
- Logic: `and`, `or`, `not`
- I/O: `print`, `println`
- Introspection: `type-of`, `describe`, `methods-of`, `type?`
- Trampolining: `bounce`, `bounce?`, `trampoline`

### Data Types (15)
`T_INT`, `T_SYM`, `T_CELL`, `T_NIL`, `T_NOTHING`, `T_PRIM`, `T_LAMBDA`, `T_MENV`, `T_CODE`, `T_ERROR`, `T_BOX`, `T_CONT`, `T_CHAN`, `T_PROCESS`, `T_BOUNCE`

---

## Priority Order for Implementation

1. **String support** - needed for any I/O, logging, user interaction
2. **Float type** - needed for scientific computing
3. **Array/dict access** - needed to work with real data
4. **Type system** - needed for organization and domain modeling
5. **FFI** - needed for BLAS/Torch integration
6. **File I/O** - needed for practical programs

---

## Note on SYNTAX.md

The `omnilisp/SYNTAX.md` file explicitly states (line 74):

> "All sections below describe the intended Omnilisp language design and are not yet implemented in the C compiler unless explicitly noted."

Sections 1.2 through 16 document **planned features**, not current implementation.
