# Omni Lisp — Complete Feature Inventory

**Last updated:** 2026-02-20

---

## 1. Special Forms

### 1.1 `lambda` — Function Definition
```lisp
(lambda (x) body)
(lambda (x y z) body)     ; multi-param (auto-curried by parser)
(lambda (x .. rest) body)  ; variadic with rest parameter
(lambda () body)            ; zero-arg lambda
```
- Multi-param lambdas auto-curry: `(lambda (x y) body)` desugars to nested single-param lambdas
- Variadic lambdas: `(lambda (x .. rest) body)`, rest collects extra args as list
- Zero-arg lambdas: `(lambda () body)` uses sentinel param, `has_param=false`
- Creates closure capturing lexical environment

### 1.2 `define` — Global Definition
```lisp
(define name value)
(define inc (lambda (x) (+ x 1)))
(define (f x) body)         ; shorthand for (define f (lambda (x) body))
(define (add a b) (+ a b))  ; shorthand with multi-param (auto-curried)
(define (thunk) 42)          ; shorthand zero-arg
```
- Binds name in global environment
- Value is evaluated before binding
- Shorthand form `(define (f x ...) body)` desugars to `(define f (lambda (x ...) body))`

### 1.3 `let` — Local Binding
```lisp
(let ((name init)) body)
(let ((x 1) (y 2)) (+ x y))  ; multi-binding (desugars to nested lets)
```
- Multi-binding let desugars to nested single-binding lets in the parser
- Non-recursive by default
- Example: `(let ((x 10)) (+ x 1))` => 11

### 1.3b `named let` — Looping Construct
```lisp
(let name ((var init) ...) body)
```
- Named let desugars to: `(let ^rec ((name (lambda (var ...) body))) (name init ...))`
- Enables iterative loops via tail-recursive named function
- Example: `(let loop ((n 5) (acc 1)) (if (= n 0) acc (loop (- n 1) (* acc n))))` => 120

### 1.4 `let ^rec` — Recursive Local Binding
```lisp
(let ^rec ((fact (lambda (n) (if (= n 0) 1 (* n (fact (- n 1)))))))
  (fact 5))
```
- Self-referencing closures via `^rec` metadata
- Creates placeholder, extends environment, then patches closure's captured env
- Enables local recursive functions without polluting global scope

### 1.5 `if` — Conditional
```lisp
(if test then-expr else-expr)
```
- All three branches required
- Short-circuit: only chosen branch is evaluated
- Truthiness: only `nil` and `false` are falsy; everything else is truthy (including `0`, `""`, `'()`)

### 1.6 `quote` — Prevent Evaluation
```lisp
(quote datum)
'datum          ; shorthand
'(1 2 3)        ; quoted list
'foo            ; quoted symbol
```

### 1.7 `and` — Short-Circuit And
```lisp
(and left right)
```
- Returns `left` if falsy (without evaluating `right`)
- Otherwise returns `right`
- Binary only (not variadic)
- Examples: `(and true 42)` => 42, `(and nil 42)` => nil

### 1.8 `or` — Short-Circuit Or
```lisp
(or left right)
```
- Returns `left` if truthy (without evaluating `right`)
- Otherwise returns `right`
- Binary only (not variadic)
- Examples: `(or 42 99)` => 42, `(or nil 99)` => 99

### 1.9 `begin` — Sequence Expression
```lisp
(begin e1 e2 ... en)
```
- Evaluates all expressions in order, returns the last
- Last expression is in tail position (TCO)
- Up to 64 expressions

### 1.10 `set!` — Variable Mutation
```lisp
(set! name value)
```
- Mutates an existing binding (local or global)
- Searches local env chain first, then global
- Error if variable is not defined

### 1.11 `quasiquote` — Template Expressions
```lisp
`(a b ,expr ,@list-expr)
```
- `` ` `` introduces a template (like `quote` but allows unquoting)
- `,expr` evaluates `expr` and splices result into template
- `,@expr` evaluates `expr` (must be list) and splices elements into template
- Supports nesting with depth tracking (Bawden's algorithm)

### 1.12 `match` — Pattern Matching
```lisp
(match expr
  (pattern1 result1)
  (pattern2 result2)
  ...)
```
- Up to 16 clauses
- Falls through on mismatch, returns nil if no match
- See Section 2 for pattern types

### 1.13 `reset` — Delimited Continuation Boundary
```lisp
(reset body)
```
- Establishes delimiter for `shift` operations
- Returns body's value normally, or shift handler's value if shifted

### 1.14 `shift` — Capture Continuation
```lisp
(shift k body)
```
- Captures continuation up to enclosing `reset` and binds to `k`
- `k` is callable: `(k value)` resumes computation with `value`
- One-shot by default in the interpreter
- Example: `(reset (+ 1 (shift k (k 10))))` => 11

### 1.15 `handle` / `perform` — Algebraic Effect Handlers
```lisp
(handle body
  ((effect-tag k arg) handler-body)
  ...)

(perform effect-tag argument)
```
- `perform` signals an effect with a tag and argument
- `handle` installs handlers that catch matching effects
- `k` is the continuation — call `(k value)` to resume with a value
- Up to 8 clauses per handle expression
- Handler result bypasses intermediate computation back to `handle`
- Example:
  ```lisp
  (handle (+ 1 (perform ask 0))
    ((ask k x) (k 10)))
  ;; => 11
  ```

### 1.16 `defmacro` — Pattern-Based Macros
```lisp
(define [macro] name
  (pattern1 template1)
  (pattern2 template2) ...)
```
- Pattern-based macros with template substitution
- Hygienic binding capture: template literal symbols (not pattern vars, not gensyms, not special forms) are resolved at macro definition time, making them immune to expansion-site shadowing
- Auto-gensym: `name#` in templates generates unique symbols for additional hygiene
- `gensym` function available for manual hygiene
- Up to 8 clauses per macro

### 1.17 Collections — Arrays, Dicts, and Generic Operations

```lisp
; Array literal
[1 2 3]                 ; desugars to (array 1 2 3)
(array 1 2 3)           ; explicit constructor
(array '(1 2 3))        ; list → array conversion

; Dict literal
{'a 1 'b 2}             ; desugars to (dict 'a 1 'b 2)
(dict 'a 1 'b 2)        ; explicit constructor

; Generic operations
(ref coll key)           ; lookup by key/index (array, dict, cons, string)
(length coll)            ; length (list, array, dict, string)
(push! arr val)          ; append to array
(keys d)                 ; dict keys as list
(values d)               ; dict values as list
(has? d key)             ; dict key existence
(remove! d key)          ; dict key removal

; Mutation
(array-set! arr idx val) ; set array element
(dict-set! d key val)    ; set dict key-value
(set! pair.car val)      ; cons cell car mutation
(set! pair.cdr val)      ; cons cell cdr mutation

; Constructor dispatch
(list [1 2 3])           ; array → list conversion
```
- Arrays are mutable dynamic arrays (contiguous memory)
- Dicts are mutable hash maps with open-addressing and linear probing
- Dict keys can be integers, strings, or symbols
- `ref` returns nil for missing keys

### 1.18 `module` / `import` — Module System
```lisp
(module name (export sym1 sym2 ...) body...)
(import name)
(import "path/to/file.omni")
```
- Modules define a namespace with explicit exports
- `import` brings exported symbols into the current environment
- File-based import with caching and circular import detection
- Auto-export: file imports without explicit module form export all defines

---

## 2. Pattern Types (for `match`)

| Pattern | Syntax | Description |
|---------|--------|-------------|
| Wildcard | `_` | Matches anything, binds nothing |
| Variable | `x` | Matches anything, binds to `x` |
| Integer literal | `42` | Matches exact integer |
| String literal | `"hello"` | Matches exact string |
| Quoted | `'symbol` | Matches quoted datum |
| Sequence (exact) | `[a b c]` | Matches list of exactly 3, binds elements |
| Head-tail | `[head .. tail]` | Binds first element and rest of list |
| Prefix | `[x y ..]` | Matches first N elements, ignores rest |
| Suffix | `[.. last]` | Skips to last element(s) |
| Constructor | `(Some x)` | Matches union variant, binds fields |
| Nullary ctor | `None` | Matches nullary constructor (auto-detected) |

Up to 16 elements per sequence pattern.

---

## 3. Data Types

| Type | Tag | Description | Examples |
|------|-----|-------------|---------|
| `nil` | NIL | Empty/false value | `nil`, `()` |
| `int` | INT | 64-bit signed integer | `42`, `-17`, `0` |
| `double` | DOUBLE | 64-bit floating point | `3.14`, `-0.5` |
| `string` | STRING | Immutable string (max 4095 characters) | `"hello"`, `"line\nbreak"` |
| `symbol` | SYMBOL | Interned identifier | `foo`, `my-function`, `null?` |
| `cons` | CONS | Pair (car/cdr) | `(cons 1 2)`, `'(1 2 3)` |
| `closure` | CLOSURE | Lambda with captured environment | `(lambda (x) x)` |
| `continuation` | CONTINUATION | Delimited continuation | captured by `shift` |
| `primitive` | PRIMITIVE | Built-in function | `+`, `car` |
| `partial_prim` | PARTIAL_PRIM | Partially applied primitive | `(+ 3)` |
| `error` | ERROR | Error value | `(error "oops")` |
| `dict` | HASHMAP | Mutable hash table | `{'a 1}`, `(dict 'a 1)` |
| `array` | ARRAY | Mutable dynamic array | `[1 2 3]`, `(array 1 2 3)` |
| `ffi_handle` | FFI_HANDLE | Foreign library handle | `(ffi-open "libc.so.6")` |
| `instance` | INSTANCE | User-defined type instance | `(Point 3 4)` |
| `method_table` | METHOD_TABLE | Multiple dispatch table | internal |

### Truthiness Rules
- **Falsy:** `nil`, `false`
- **Truthy:** Everything else, including `0`, `""`, and `'()`

---

## 4. Syntax Extensions

### 4.1 Dot-Bracket Indexing
```lisp
list.[0]         ; first element of list
arr.[0]          ; first element of array
dict.['key]      ; dict key lookup
str.[2]          ; character code at index 2
matrix.[i].[j]   ; chained indexing
```
- Works on lists, arrays, dicts, and strings
- Index is any expression
- Chainable for nested access

### 4.2 Path Notation (Field Access)
```lisp
point.x                  ; struct field access
person.address.city      ; chained field access
pair.car                 ; cons cell car access
pair.cdr                 ; cons cell cdr access
```
- Struct instances use field names
- Cons cells support `.car` and `.cdr` as special fields
- Other values fall back to association list (alist) lookup
- Up to 8 segments deep

### 4.3 Quote Shorthand
```lisp
'x        ; => (quote x)
'(a b c)  ; => (quote (a b c))
```

### 4.4 Escape Sequences in Strings
```
\n    newline
\t    tab
\\    backslash
\"    double quote
```

### 4.5 Comments
```lisp
; This is a comment (extends to end of line)
```

---

## 5. Primitive Functions

### 5.1 Arithmetic (5)
| Primitive | Arity | Description |
|-----------|-------|-------------|
| `+` | variadic | Addition; `(+ n)` returns n |
| `-` | 1-2 | Subtraction; `(- n)` negates |
| `*` | 2 | Multiplication |
| `/` | 2 | Integer division (error on div by zero) |
| `%` | 2 | Modulo (error on div by zero) |

Binary arithmetic primitives auto-curry: `(+ 3)` returns a function that adds 3.

### 5.2 Comparison (5)
| Primitive | Arity | Description |
|-----------|-------|-------------|
| `=` | 2 | Structural equality |
| `<` | 2 | Less than |
| `>` | 2 | Greater than |
| `<=` | 2 | Less or equal |
| `>=` | 2 | Greater or equal |

Returns `true` or `nil`.

### 5.3 List Operations (7)
| Primitive | Arity | Description |
|-----------|-------|-------------|
| `cons` | 2 | Construct pair |
| `car` | 1 | First element of pair |
| `cdr` | 1 | Rest of pair |
| `list` | variadic | Create list; `(list [1 2 3])` converts array to list |
| `length` | 1 | Generic: list, array, dict, or string length |
| `null?` | 1 | Test if nil |
| `pair?` | 1 | Test if cons cell |

### 5.4 Boolean (1)
| Primitive | Arity | Description |
|-----------|-------|-------------|
| `not` | 1 | Logical negation |

### 5.5 I/O (3 + effect wrappers)
| Primitive | Arity | Description |
|-----------|-------|-------------|
| `print` | 1 | Output without newline (via `io/print` effect) |
| `println` | 1 | Output with newline (via `io/println` effect) |
| `newline` | 0 | Output newline only (via `io/newline` effect) |
| `display` | 1 | Display value (via `io/display` effect) |

I/O primitives go through algebraic effects. Raw versions (`__raw-print`, `__raw-println`, etc.) bypass effects.
When no handler is installed, a fast path calls raw primitives directly (zero overhead).

### 5.6 String Operations (10)
| Primitive | Arity | Description |
|-----------|-------|-------------|
| `string-append` | variadic | Concatenate strings |
| `string-join` | 2 | Join list with separator |
| `substring` | 3 | Extract substring (supports negative indices) |
| `string-split` | 2 | Split by single-char delimiter |
| `string-length` | 1 | String length |
| `string->list` | 1 | String to list of single-char strings |
| `list->string` | 1 | List of strings to string |
| `string-upcase` | 1 | Uppercase conversion |
| `string-downcase` | 1 | Lowercase conversion |
| `string-trim` | 1 | Remove leading/trailing whitespace |

### 5.7 Type Predicates (5)
| Primitive | Arity | Description |
|-----------|-------|-------------|
| `string?` | 1 | Is value a string? |
| `int?` | 1 | Is value an integer? |
| `symbol?` | 1 | Is value a symbol? |
| `closure?` | 1 | Is value a closure? |
| `continuation?` | 1 | Is value a continuation? |

### 5.8 File I/O (5)
| Primitive | Arity | Description |
|-----------|-------|-------------|
| `read-file` | 1 | Read entire file as string |
| `write-file` | 2 | Write string to file |
| `file-exists?` | 1 | Test if file exists |
| `read-lines` | 1 | Read file as list of lines |
| `load` | 1 | Load and evaluate an Omni file |

### 5.9 Constants (2)
| Name | Value |
|------|-------|
| `true` | Symbol `true` |
| `false` | Bound to `nil` |

**Total: 129+ primitives + 2 constants** (includes math library, bitwise ops, sorting, introspection, type system, generic collection ops, and more not listed above)

### 5.10 FFI (Foreign Function Interface)
| Primitive | Arity | Description |
|-----------|-------|-------------|
| `ffi-open` | 1 | Open shared library via dlopen |
| `ffi-call` | variadic | Call foreign function with type annotations |
| `ffi-close` | 1 | Close shared library handle |
| `ffi-sym` | 2 | Get raw function pointer as integer |

**FFI type symbols:** `'int`, `'size`, `'string`, `'void`, `'ptr`, `'double`

```lisp
(define libc (ffi-open "libc.so.6"))
(ffi-call libc "strlen" 'size "hello" 'string)   ;; => 5
(ffi-call libc "abs" 'int -42 'int)              ;; => 42
(ffi-call libc "getpid" 'int)                     ;; => <pid>
(ffi-call libc "atoi" 'int "12345" 'string)       ;; => 12345
(ffi-sym libc "strlen")                            ;; => <pointer as int>
(ffi-close libc)
```

- Up to 6 C arguments per call
- x86_64 ABI: all args passed as `long` (pointer-sized), cast via type annotations
- No libffi dependency — uses function pointer casting (same approach as OmniLisp)
- Handles allocated in root_region (survive REPL line reclamation)
- Error on bad library or bad symbol returns error value
- Supported in interpreter, JIT (via fallback), and compiler

---

## 6. Continuation & Effect System (Low-Level)

### 6.1 Delimited Continuations
- `reset`/`shift` based (not call/cc)
- One-shot by default, multi-shot via `clone_continuation()`
- Stack captured by copying (x86_64 register save/restore)
- Continuations invalidated when their home region dies
- Thread-local prompt stacks

### 6.2 Effect Handlers
- Installed on prompt frames
- `perform` searches handler stack top-to-bottom
- Handler receives continuation `k`, can resume or abort
- Handler result bypasses intermediate computation back to `handle`

### 6.3 What Omni Does NOT Have
- **No call/cc** — uses delimited continuations instead
- **No garbage collection** — uses region-based memory with per-REPL-line reclamation
- **No continuations across threads** — continuations are thread-local

### 6.4 Tail-Call Optimization
Omni implements TCO via an eval loop with `continue` for tail positions:
- `if` — both branches are tail positions
- `let` — body is tail position
- `begin` — last expression is tail position
- `and`/`or` — second operand is tail position
- `match` — clause bodies are tail positions
- Function application — tail calls reuse the eval loop
- Macro expansion — re-evaluates expanded form in tail position

---

## 7. Compiler Support

The Omni compiler (`src/lisp/compiler.c3`) translates Lisp AST to C3 source code using a runtime library (`src/lisp/runtime.c3`).

| Feature | Interpreter | Compiler |
|---------|:-----------:|:--------:|
| lambda | Y | Y |
| define | Y | Y |
| let | Y | Y |
| let ^rec | Y | Y |
| if | Y | Y |
| quote | Y | Y |
| match | Y | Y |
| and/or | Y | Y |
| reset/shift | Y | N |
| handle/perform | Y | N |
| dot-bracket `.[i]` | Y | ? |
| path `a.b.c` | Y | ? |

---

## 8. Evaluation Model

### Application
- `(f a b c)` parsed as `E_CALL` with func + args list (not curried)
- Primitives receive all args at once
- Closures applied one-at-a-time via currying
- Up to 64 arguments per call

### Scoping
- Lexical scoping with closure-based environments
- Linked-list environment frames (up to 256 bindings per frame)
- Global environment searched after local

### Memory
- Region-based allocation for all Lisp objects (values, envs, exprs, patterns, continuations)
- Per-REPL-line temp regions: push child region before eval, pop after (frees temporaries)
- Closure env promotion: `deep_copy_env()` copies captured environments to root region
- Global defines copied to root region to survive temp frame release
- Hash map entries use `mem::malloc` for contiguous array indexing

---

## 9. Type System

### 9.1 Type Definitions
```lisp
(define [type] Point (^Int x) (^Int y))           ;; struct type
(define [abstract] Shape)                           ;; abstract type
(define [type] (Circle Shape) (^Int radius))       ;; subtype of Shape
(define [union] (Option T) None (Some T))          ;; union/ADT
(define [alias] Num Int)                            ;; type alias
```

### 9.2 Multiple Dispatch
```lisp
(define (describe (^Int n)) "integer")
(define (describe (^String s)) "string")
(define (describe x) "other")           ;; fallback
(describe 42)      ;; => "integer"
(describe "hi")    ;; => "string"
```

Val dispatch for value-level matching:
```lisp
(define (fib (^(Val 0) n)) 0)
(define (fib (^(Val 1) n)) 1)
(define (fib (^Int n)) (+ (fib (- n 1)) (fib (- n 2))))
(fib 10)  ;; => 55
```

### 9.3 Type Introspection
| Primitive | Arity | Description |
|-----------|-------|-------------|
| `type-of` | 1 | Returns type name as symbol |
| `is?` | 2 | Check if value is/subtypes given type |
| `instance?` | 1 | Check if value is a type instance |

### 9.4 Struct Features
- Field access via dot-path: `point.x`
- Nested access: `line.start.x`
- Field mutation: `(set! point.x 99)`
- Positional indexing: `instance.[0]`
- Instance construction: `(Point 3 4)`

---

## 10. JIT Compilation

GNU Lightning-based JIT compiler (`src/lisp/jit.c3`):
- Compiles expressions to native x86_64 machine code
- Falls back to interpreter for complex forms (effects, macros, modules, types)
- Every test runs both interpreter AND JIT for parity verification
- Supports: lambda, if, let, define, quote, call, arithmetic, comparison, list ops, begin, set!

---

## 11. Lisp-to-C3 Compiler

Transpiler (`src/lisp/compiler.c3`) generates C3 source code:
- Runtime library (`src/lisp/runtime.c3`) provides value representation
- TCO via V_THUNK trampoline pattern
- Delegates complex forms to interpreter via `rt_eval_source()`
- Supports: lambda, if, let (incl. ^rec), define, match, begin, and/or, call, literals

---

## 12. Limits

| Resource | Limit |
|----------|-------|
| Symbol name length | 64 bytes |
| Total symbols | 512 |
| Bindings per env frame | 512 |
| Values | Region-allocated (no fixed pool) |
| Environments | Region-allocated (no fixed pool) |
| Expressions | Region-allocated (no fixed pool) |
| Patterns | Region-allocated (no fixed pool) |
| Continuations | Region-allocated (no fixed pool) |
| Match clauses | 16 |
| Effect handler clauses | 8 |
| Handler stack depth | 16 |
| Call arguments | 64 |
| Path segments | 8 |
| Pattern elements | 16 |
| Macros | 64 |
| Modules | 32 |
| Macro clauses | 8 per macro |
| Module exports | 128 per module |
| Begin expressions | 64 |
| Lambda params | 64 |
| String | 4095 characters (4096 bytes max) |
| Eval depth | 200 (stack overflow guard) |
