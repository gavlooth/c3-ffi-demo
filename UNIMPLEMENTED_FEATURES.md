# OmniLisp Unimplemented Features

This document tracks features that are documented in the language design but not yet implemented in the runtime/compiler.

---

## Critical Gaps (Blocking Production Use)

### ~~1. Strings & Characters~~ ✓ IMPLEMENTED

**Status:** T_STRING and T_CHAR types implemented

**Implemented:**
- String literals: `"hello world"`
- Character literals: `#\a`, `#\newline`, `#\space`, `#\tab`
- String operations: `string-length`, `string-append`, `substring`, `string-ref`
- Conversion: `string->list`, `list->string`
- Predicates: `string?`, `char?`, `empty?`

**Remaining:**
- String interpolation (parsing exists but runtime not wired)
- Full UTF-8 support (currently byte-oriented)

---

### ~~2. Float Type & Numeric Operations~~ ✓ IMPLEMENTED

**Status:** T_FLOAT type implemented with full math library

**Implemented:**
- Float literals: `3.14`, `1.0e-5`
- Hex/binary literals: `0xFF`, `0b1010`
- Predicates: `float?`, `int?`, `number?`
- Trigonometry: `sin`, `cos`, `tan`, `asin`, `acos`, `atan`, `atan2`
- Exponentials: `exp`, `log`, `log10`, `sqrt`, `pow`
- Rounding: `floor`, `ceil`, `round`, `truncate`, `abs`
- Conversion: `->int`, `->float`
- Constants: `pi`, `e`, `inf`, `nan`
- Special: `nan?`, `inf?`
- Mixed arithmetic: ints and floats work together seamlessly

---

### ~~3. Array & Dict Access/Mutation~~ ✓ IMPLEMENTED

**Status:** Full primitive access implemented

**Implemented:**
```scheme
(array 1 2 3)              ; creates array
(array? arr)               ; predicate
(array-ref arr 0)          ; get element at index
(array-set! arr 0 val)     ; set element at index
(array-length arr)         ; get length
(array-slice arr 1 4)      ; get slice [1:4]

(dict :a 1 :b 2)           ; creates dict
(dict? d)                  ; predicate
(dict-ref d :a)            ; get value for key
(dict-set! d :a val)       ; set value for key
(dict-contains? d :a)      ; check if key exists
(keys d)                   ; get all keys
(values d)                 ; get all values
```

**Dot Access Syntax:** ✓ IMPLEMENTED
```scheme
;; Field access: obj.field -> (get obj :field)
person.name              ; -> (get person :name)
company.ceo.name         ; chained: a.b.c

;; Computed key: obj.(expr) -> (get obj expr)
arr.(0)                  ; array index
arr.(idx)                ; variable as key
data.("string-key")      ; string key
obj.(:key)               ; explicit keyword (same as obj.key)

;; Slicing
arr.[1 4]                ; -> (array-slice arr 1 4)

;; Works on arrays, dicts, strings
"hello".(0)              ; -> #\h
```

---

### 4. Type System Features

**Structs:** ✓ IMPLEMENTED
```scheme
;; Define a struct
(define {struct Point} [x] [y])

;; Constructor is created automatically
(define p (Point 10 20))

;; Field access with dot syntax
p.x              ; -> 10
p.y              ; -> 20

;; Predicate is created automatically
(Point? p)       ; -> true
(Point? 42)      ; -> false

;; Works with .field getters
(map .x points)  ; extract x from list of Points
```

**Enums:** ✓ IMPLEMENTED (Simple + Data Variants)
```scheme
;; Simple enum (no data)
(define {enum Color}
  Red Green Blue)

;; Each variant is a value with metadata
Red               ; -> dict with __enum__, __variant__, __index__
(get Red '__enum__)     ; -> Color
(get Red '__variant__)  ; -> Red
(get Red '__index__)    ; -> 0

;; Type predicate
(Color? Red)      ; -> true
(Color? 42)       ; -> false

;; Per-variant predicates
(Red? Red)        ; -> true
(Green? Red)      ; -> false

;; Enums are comparable
(= Red Red)       ; -> true
(= Red Green)     ; -> false

;; EnumName holds list of variant symbols
Color             ; -> (Red Green Blue)
```

**Enums with Data:** ✓ IMPLEMENTED
```scheme
;; Define enum with data variants
(define {enum Option}
  (Some [value])    ; Data variant with one field
  None)             ; Simple variant

;; Use the constructor (data variants are functions)
(define x (Some 42))
(get x 'value)      ; -> 42

;; Predicates work on all variants
(Option? x)         ; -> true
(Some? x)           ; -> true
(None? x)           ; -> false
(None? None)        ; -> true

;; Result type with error handling
(define {enum Result}
  (Ok [value])
  (Err [message]))

(define success (Ok 100))
(define failure (Err "not found"))

(Ok? success)       ; -> true
(Err? failure)      ; -> true

;; Pattern matching extracts data
(match x
  [(Some v) v]      ; Destructures Some, binds value to v
  [None "default"]) ; -> 42

(match failure
  [(Ok v) v]
  [(Err msg) msg])  ; -> "not found"

;; Direct field access
(get (Some 42) 'value)  ; -> 42
```

**Algebraic Effects:** ✓ IMPLEMENTED (OCaml 5-style)
```scheme
;; Define an effect with mode and optional types
(define {effect ask} :one-shot)                    ; basic
(define {effect ask} :one-shot (returns Answer))   ; with return type
(define {effect fetch} :one-shot
  (payload String)
  (returns String))                                ; with both types

;; Recovery modes:
;;   :one-shot    - resume at most once (default)
;;   :multi-shot  - resume multiple times
;;   :abort       - never resumes (like exceptions)
;;   :tail        - tail-resumptive (optimized)

;; Handle effects with perform/resume
(handle
  (+ 1 (perform ask nothing))
  (ask (payload resume) (resume 41)))  ; -> 42

;; Abort effects for error handling
(define {effect fail} :abort (payload String))
(handle
  (do (println "before") (perform fail "oops"))
  (fail (msg _) (string-append "Caught: " msg)))  ; -> "Caught: oops"
```

**Default Parameters:** ✓ IMPLEMENTED
```scheme
;; Optional parameter with default value
(define (greet [name "Guest"])
  (string-append "Hello, " name "!"))

(greet)          ; -> "Hello, Guest!"
(greet "Alice")  ; -> "Hello, Alice!"

;; Multiple defaults
(define (make-point [x 0] [y 0]) (list x y))
(make-point)       ; -> (0 0)
(make-point 5)     ; -> (5 0)
(make-point 5 10)  ; -> (5 10)

;; Mixed required and optional
(define (greet-with-title title [name "Guest"])
  (string-append title " " name))

;; Default expressions (evaluated at call time)
(define base 100)
(define (add-to-base [n base]) (+ n 10))
```

**Still Missing:**
```scheme
; Parametric types
{struct [Pair T]}

; Type annotations
(define (add [x int] [y int]) -> int
  (+ x y))

; Interfaces
(define {interface Printable}
  [print (-> self String)])
```

**Impact:** Parametric types and interfaces still needed for full type system

---

### ~~5. FFI & External Handles~~ ✓ IMPLEMENTED

**Status:** Basic FFI implemented with dlopen/dlsym

**Implemented:**
```scheme
;; Load a shared library
(define libm (ffi/load "libm.so.6"))

;; Get a symbol (function pointer)
(define sin-fn (ffi/symbol libm "sin"))

;; Call foreign function with type signature
;; Signature: (-> arg-types... return-type)
;; Supported types: int, double, void, ptr, string
(ffi/call sin-fn '(-> double double) 1.57)     ; sin(pi/2) -> 1.0
(ffi/call pow-fn '(-> double double double) 2 10) ; pow(2,10) -> 1024

;; Memory operations
(define buf (ffi/malloc 100))    ; allocate memory
(ffi/ptr-write-int buf 42)       ; write int to pointer
(ffi/ptr-read-int buf)           ; read int from pointer
(ffi/ptr-write-double buf 3.14)  ; write double
(ffi/ptr-read-double buf)        ; read double
(ffi/ptr-read-string buf)        ; read C string
(ffi/free buf)                   ; free memory

;; Type predicates
(ffi/lib? x)           ; check if FFI library
(ffi/ptr? x)           ; check if FFI pointer
(ffi/null? ptr)        ; check if null pointer
(ffi/null)             ; create null pointer
(ffi/ptr-address ptr)  ; get raw address (debugging)

;; Cleanup
(ffi/close libm)       ; close library handle
```

**Remaining:**
```scheme
;; High-level extern declarations (syntax sugar)
(define {extern puts :from libc} [s {CString}] -> {CInt})

;; Opaque types with destructors
(define {opaque SDL_Window :destructor SDL_DestroyWindow})

;; Callbacks from C into OmniLisp
(ffi/register-callback (lambda (x) ...))
```

---

### ~~6. File I/O~~ ✓ IMPLEMENTED

**Status:** Full file I/O primitives implemented

**Implemented:**
```scheme
(open "file.txt" :read)      ; open for reading
(open "file.txt" :write)     ; open for writing
(open "file.txt" :append)    ; open for appending
(read-line port)             ; read line (returns nothing at EOF)
(read-all port)              ; read entire file
(write-string port "data")   ; write string
(write-line port "data")     ; write line with newline
(flush port)                 ; flush output
(close port)                 ; close port
(port? x)                    ; predicate
(eof? port)                  ; check for EOF
(file-exists? "file.txt")    ; check if file exists
```

**Remaining:**
```scheme
(with-open-file [f "file.txt"] ...)  ; auto-closing macro
```

---

## Partial Implementation (Forms Exist But Incomplete)

### ~~Modules~~ ✓ IMPLEMENTED

**Status:** Full module system with namespace isolation

**Implemented:**
```scheme
;; Define a module with exports
(module math
  (export square cube)
  (define (square x) (* x x))
  (define (cube x) (* x x x))
  (define (private-fn x) x))  ; not exported

;; Import all exports
(import math)
(square 4)  ; -> 16

;; Import specific symbols
(import math :only [square])

;; Import with alias
(import math :as m)
```

**Features:**
- Namespace isolation (private functions stay private)
- Export declarations within module body
- Selective imports with `:only`
- Aliased imports with `:as`

---

### ~~Concurrency~~ ✓ IMPLEMENTED

**Status:** Fibers and system threads fully implemented

**Fibers (cooperative concurrency):**
```scheme
(fiber (lambda () ...))  ; create fiber
(spawn f)                ; add fiber to scheduler
(resume f)               ; resume fiber, get yielded value
(yield)                  ; yield control, return to resumer
(yield val)              ; yield with value
(join f)                 ; wait for fiber to complete
(chan n)                 ; create buffered channel
(send ch val)            ; send to channel (may block)
(recv ch)                ; receive from channel (may block)
(with-fibers body...)    ; scoped fiber execution with auto-cleanup
```

**Threads (true parallelism):**
```scheme
(thread (lambda () ...))  ; create and start system thread
(thread-join t)           ; wait for thread to complete, get result
(thread? x)               ; predicate
(thread-done? t)          ; check if thread has completed
(thread-result t)         ; get result without blocking (returns nothing if not done)
```

**Atomic Operations:**
```scheme
(atomic-ref box)          ; atomically read box value
(atomic-set! box val)     ; atomically set box value
(atomic-cas! box old new) ; compare-and-swap, returns true if swapped
(atomic-swap! box fn)     ; atomically apply fn to box contents
```

**Box Primitives (mutable cells):**
```scheme
(box val)                 ; create mutable reference cell
(box? x)                  ; predicate
(box-ref b)               ; get box value
(box-set! b val)          ; set box value
```

**Architecture:**
- Fibers: ucontext-based cooperative concurrency
- Threads: pthread-based true parallelism
- Atomics: mutex-protected operations for thread safety

---

### ~~Metadata~~ ✓ IMPLEMENTED

**Status:** Full metadata support with reader syntax and primitives

**Implemented:**
```scheme
;; Attach metadata with reader syntax
(define ^:private secret-fn (lambda (x) (* x 2)))
(define ^:deprecated old-fn (lambda () "use new-fn"))
(define ^"Adds two numbers" add-fn (lambda (a b) (+ a b)))

;; Query metadata
(meta x)              ; -> get metadata (symbol, string, or nil)
(private? x)          ; -> true if ^:private
(deprecated? x)       ; -> true if ^:deprecated
(doc x)               ; -> docstring if ^"..." metadata

;; Modify metadata
(set-meta! x 'some-meta)   ; set metadata on x
(has-meta? x 'private)     ; check for specific key
(get-meta x 'key default)  ; get specific key with default
```

**Features:**
- `^:keyword` - attaches quoted symbol as metadata
- `^"string"` - attaches string as docstring
- `^{...}` - attaches quoted map (dict) as metadata
- Metadata is stored in a global table keyed by value pointer
- Works with define for both variables and functions

---

## Not Started (Design Only)

### ~~Destructuring in Bindings~~ ✓ IMPLEMENTED

**Status:** Destructuring works in let bindings

**Implemented:**
```scheme
(let [[x y z] (list 1 2 3)] ...)     ; array/list destructuring
(let [(cons h t) my-list] ...)       ; cons destructuring
(let [[a [b c]] nested] ...)         ; nested destructuring
```

**Remaining:**
```scheme
(define [a b c] [1 2 3])             ; top-level define destructuring
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
```

---

### Reader Macros

```scheme
#_expr           ; discard form
#|block comment|#
#uuid"..."       ; typed literals
#path"..."
```

---

### ~~Symbols & Quote Sugar~~ ✓ IMPLEMENTED

**Status:** `:foo` is sugar for `'foo` (quoted symbol)

```scheme
:foo             ; -> foo (sugar for 'foo)
'foo             ; -> foo (quoted symbol)
(= :foo 'foo)    ; -> true (same thing)
(symbol? :foo)   ; -> true
(symbol? 'bar)   ; -> true

;; .field is a getter function
.name            ; -> (lambda (x) (get x 'name))
(map .name people)  ; extract field from list
```

---

### ~~Pipe Operators~~ ✓ IMPLEMENTED

```scheme
;; Thread value through functions (inserts as first argument)
(|> 10 (+ 5) (* 2))   ; -> ((10+5)*2) = 30

;; With 'it' or '_' placeholder for custom position
(|> 10 (- 100 it))    ; -> (- 100 10) = 90

;; Works with any functions
(|> (range 1 6)
    (filter (lambda (x) (= 0 (% x 2))) it)  ; evens
    (map (lambda (x) (* x x)) it)           ; square
    (reduce + 0 it))                         ; sum -> 20

;; Just function name (no extra args)
(|> "hello" string-length)  ; -> 5

;; 'pipe' is an alias for '|>'
(pipe 5 (+ 10))       ; -> 15
```

**Features:**
- Thread-first by default (value inserted as first argument)
- `it` or `_` placeholder for custom positioning
- Multiple placeholders in same form
- Nested placeholders work correctly
- `pipe` alias for ASCII-only environments

---

### Try/Catch/Finally

```scheme
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
- **Core pattern matching:** `match` (THE core primitive - `if`/`cond` should be macros over match)
- Control flow:  `if`, `do`/`begin`, `when`, `unless`, `cond`
- Bindings: `let`, `define`, `set!`
- Functions: `lambda`, `fn`
- Collections: `array`, `dict` (with full access/mutation primitives)
- Iteration: `for`, `foreach`
- Delimited continuations: `prompt`, `control`
- Algebraic effects: `(define {effect name} mode)`, `handle`, `perform`, `resume` (also legacy `defeffect`)
- Conditions/restarts: `handler-case`, `handler-bind`, `restart-case`, `with-restarts`, `call-restart`
- Debugging: `call-stack`, `stack-trace`, `effect-trace`, `effect-stack`

### Concurrency Primitives
- Fibers: `fiber`, `spawn`, `resume`, `yield`, `join`, `with-fibers`
- Channels: `chan`, `send`, `recv`
- Threads: `thread`, `thread-join`, `thread?`, `thread-done?`, `thread-result`
- Atomics: `atomic-ref`, `atomic-set!`, `atomic-cas!`, `atomic-swap!`
- Boxes: `box`, `box?`, `box-ref`, `box-set!`

### Primitives (80+)
- Arithmetic: `+`, `-`, `*`, `/`, `%`
- Comparison: `<`, `>`, `<=`, `>=`, `=`
- Lists: `cons`, `car`, `cdr`, `empty?`, `list`, `length`
- Arrays: `array?`, `array-ref`, `array-set!`, `array-length`, `array-slice`
- Dicts: `dict?`, `dict-ref`, `dict-set!`, `dict-contains?`, `keys`, `values`
- Strings: `string?`, `char?`, `keyword?`, `string-length`, `string-append`, `string-ref`, `substring`, `string->list`, `list->string`
- Math: `float?`, `int?`, `number?`, `sin`, `cos`, `tan`, `asin`, `acos`, `atan`, `atan2`, `exp`, `log`, `log10`, `sqrt`, `pow`, `abs`, `floor`, `ceil`, `round`, `truncate`, `->int`, `->float`, `pi`, `e`, `inf`, `nan`, `nan?`, `inf?`
- File I/O: `open`, `close`, `read-line`, `read-all`, `write-string`, `write-line`, `flush`, `port?`, `eof?`, `file-exists?`
- Higher-order: `map`, `filter`, `reduce`, `range`
- Logic: `and`, `or`, `not`
- I/O: `print`, `println`
- Introspection: `type-of`, `describe`, `methods-of`, `type?`
- Trampolining: `bounce`, `bounce?`, `trampoline`
- FFI: `ffi/load`, `ffi/close`, `ffi/symbol`, `ffi/call`, `ffi/lib?`, `ffi/ptr?`, `ffi/null?`, `ffi/null`, `ffi/ptr-address`, `ffi/malloc`, `ffi/free`, `ffi/ptr-read-int`, `ffi/ptr-write-int`, `ffi/ptr-read-double`, `ffi/ptr-write-double`, `ffi/ptr-read-string`
- Metadata: `meta`, `set-meta!`, `has-meta?`, `get-meta`, `private?`, `deprecated?`, `doc`
- Threads: `thread`, `thread-join`, `thread?`, `thread-done?`, `thread-result`
- Atomics: `atomic-ref`, `atomic-set!`, `atomic-cas!`, `atomic-swap!`
- Boxes: `box`, `box?`, `box-ref`, `box-set!`

### Data Types (23)
`T_INT`, `T_SYM`, `T_CELL`, `T_NIL`, `T_NOTHING`, `T_PRIM`, `T_LAMBDA`, `T_MENV`, `T_CODE`, `T_ERROR`, `T_BOX`, `T_CONT`, `T_CHAN`, `T_PROCESS`, `T_BOUNCE`, `T_STRING`, `T_CHAR`, `T_FLOAT`, `T_PORT`, `T_SYNTAX`, `T_FFI_LIB`, `T_FFI_PTR`, `T_THREAD`

---

## Architecture Notes

### ~~Match as Core Primitive~~ ✓ IMPLEMENTED

`match` is the fundamental control flow primitive implemented at the C level. Other control flow forms can now be defined as macros using `(define [syntax ...])`:

```scheme
;; Hygienic macro definition with (define [syntax name] ...)
(define [syntax my-if]
  [(my-if test then else)
   (match test [true then] [_ else])])

;; cond with literals declaration
(define [syntax my-cond]
  (literals else)
  [(my-cond) nothing]
  [(my-cond [else e]) e]
  [(my-cond [test e] rest ...)
   (my-if test e (my-cond rest ...))])

;; and/or with ellipsis patterns
(define [syntax my-and]
  [(my-and) true]
  [(my-and x) x]
  [(my-and x y ...) (if x (my-and y ...) false)])
```

**Status:** Hygienic macro system fully implemented:
- `(define [syntax name] ...)` - define syntax transformer
- Pattern matching with literals: `(literals else =>)`
- Ellipsis patterns: `x ...` for zero-or-more matching
- Template expansion with variable substitution

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
