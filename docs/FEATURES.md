# Omni Lisp — Complete Feature Inventory

**Last updated:** 2026-02-28

---

## 1. Special Forms

### 1.1 `lambda` — Function Definition
```lisp
(lambda (x) body)
(lambda (x y z) body)       ; multi-param with strict arity
(lambda (x .. rest) body)    ; variadic with rest parameter
(lambda () body)              ; zero-arg lambda
(lambda ({name age}) body)   ; dict destructuring parameter
(lambda ({x y} z) body)     ; mixed dict + positional params
```
- Multi-param lambdas have strict arity: `(lambda (x y) body)` requires exactly 2 arguments
- Use `_` placeholder `(+ 1 _)`, `|>` pipe, or `partial` for partial application
- Variadic lambdas: `(lambda (x .. rest) body)`, rest collects extra args as list
- Zero-arg lambdas: `(lambda () body)` uses sentinel param, `has_param=false`
- Dict destructuring params: `{name age}` extracts `'name` and `'age` keys from a dict arg
- Creates closure capturing lexical environment

### 1.2 `define` — Global Definition
```lisp
(define name value)
(define inc (lambda (x) (+ x 1)))
(define (f x) body)         ; shorthand for (define f (lambda (x) body))
(define (add a b) (+ a b))  ; shorthand with multi-param (strict arity)
(define (thunk) 42)          ; shorthand zero-arg
```
- Binds name in global environment
- Value is evaluated before binding
- Shorthand form `(define (f x ...) body)` desugars to `(define f (lambda (x ...) body))`
- Dict destructuring params: `(define (f {x y} z) body)` — desugars to let-destruct in body
- `(define [...] ...)` with brackets is reserved for attributes (`[type]`, `[ffi lib]`, etc.)

### 1.3 `let` — Local Binding
```lisp
(let (name init) body)
(let (x 1 y 2) (+ x y))                     ; multi-binding (desugars to nested lets)
(let ([x y] [10 20]) (+ x y))               ; array destructuring
(let ([head .. tail] '(1 2 3)) head)         ; head/tail destructuring
(let ({name age} {'name "Alice" 'age 30}) name) ; dict destructuring
(let ([a b] [3 4] z 5) (+ a (+ b z)))       ; mixed plain + destructuring
```
- Flat-pair syntax: `(let (x v) body)`, `(let (x 1 y 2) body)`
- Multi-binding let desugars to nested single-binding lets in the parser
- Array destructuring: `[x y]`, `[head .. tail]`, `[a b ..]` — works on both lists and arrays
- Dict destructuring: `{name age}` — extracts `'name` and `'age` keys, binds to local vars
- Non-recursive by default
- Example: `(let (x 10) (+ x 1))` => 11

### 1.3b `named let` — Looping Construct
```lisp
(let name (var init ...) body)
```
- Named let desugars to: `(let ^rec (name (lambda (var ...) body)) (name init ...))`
- Enables iterative loops via tail-recursive named function
- Example: `(let loop (n 5 acc 1) (if (= n 0) acc (loop (- n 1) (* acc n))))` => 120

### 1.4 `let ^rec` — Recursive Local Binding
```lisp
(let ^rec (fact (lambda (n) (if (= n 0) 1 (* n (fact (- n 1))))))
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
- Dynamic expression count (no fixed limit)

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
- Dynamic clause count (no fixed limit)
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

### 1.15 `handle` / `signal` — Algebraic Effect Handlers
```lisp
(handle body
  (effect-tag arg (resolve expr))   ; resuming handler
  (effect-tag arg expr)             ; aborting handler
  ...)

(signal effect-tag argument)
```
- `signal` signals an effect with a tag and argument
- `handle` installs handlers that catch matching effects
- `(resolve expr)` resumes the continuation with the value of `expr`
- Omitting `resolve` aborts — the handler's result replaces the entire `handle` expression
- Dynamic clause count (no fixed limit)
- Handler result bypasses intermediate computation back to `handle`
- Example:
  ```lisp
  (handle (+ 1 (signal ask 0))
    (ask x (resolve 10)))
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

;; Qualified access (default)
(import name)              ; binds module as value, access via name.sym
name.sym1                  ; qualified access

;; Selective import
(import name (sym1 sym2))  ; imports specific symbols unqualified
(import name (sym1 :as alias))  ; rename on import

;; Full namespace import
(import name :all)         ; imports ALL exports unqualified

;; File-based import
(import "path/to/file.omni")

;; Re-export
(export-from name (sym1 sym2))  ; re-export specific symbols
(export-from name :all)         ; re-export everything
```
- Default import is **qualified-only**: `(import mod)` binds module as value, access via `mod.sym`
- Selective import brings specific symbols into scope unqualified
- `:all` opt-in for importing all exports unqualified
- File-based import with caching and circular import detection
- Auto-export: file imports without explicit module form export all defines
- Method extensions are always global (dispatch is cross-cutting)

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

Dynamic element count per pattern (no fixed limit).

---

## 3. Data Types

| Type | Tag | Description | Examples |
|------|-----|-------------|---------|
| `nil` | NIL | Empty/false value | `nil`, `()` |
| `int` | INT | 64-bit signed integer | `42`, `-17`, `0` |
| `double` | DOUBLE | 64-bit floating point | `3.14`, `-0.5` |
| `string` | STRING | Immutable string (heap-allocated) | `"hello"`, `"line\nbreak"` |
| `symbol` | SYMBOL | Interned identifier | `foo`, `my-function`, `null?` |
| `cons` | CONS | Pair (car/cdr) | `(cons 1 2)`, `'(1 2 3)` |
| `closure` | CLOSURE | Lambda with captured environment | `(lambda (x) x)` |
| `continuation` | CONTINUATION | Delimited continuation | captured by `shift` |
| `primitive` | PRIMITIVE | Built-in function | `+`, `car` |
| `partial_prim` | PARTIAL_PRIM | Partially applied primitive | `(+ 3)` |
| `error` | ERROR | Error value | `(error "oops")` |
| `dict` | HASHMAP | Mutable hash table | `{'a 1}`, `(dict 'a 1)` |
| `array` | ARRAY | Mutable dynamic array | `[1 2 3]`, `(array 1 2 3)` |
| `coroutine` | COROUTINE | User-level coroutine | `(coroutine (lambda () body))` |
| `ffi_handle` | FFI_HANDLE | Foreign library handle | `(define [ffi lib] libc "libc.so.6")` |
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

Binary primitives (`+`, `-`, `*`, `/`, `%`, comparisons, `cons`) partially apply when given one argument: `(+ 3)` returns a `PARTIAL_PRIM` that adds 3. This is a built-in mechanism for binary primitives only — user-defined lambdas have strict arity. For general partial application, use `_` placeholder `(+ 1 _)`, `|>` pipe, or `partial`.

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

#### Declarative FFI (recommended)
```lisp
;; Declare a library handle
(define [ffi lib] libc "libc.so.6")

;; Bind a C function as a native Omni function
(define [ffi λ libc] (strlen (^String s)) ^Int)
(define [ffi λ libc] (abs (^Int n)) ^Int)

(strlen "hello")  ;; => 5
(abs -42)          ;; => 42
```

- Uses libffi via C wrapper (`csrc/ffi_helpers.c`) for portable ABI support
- Type annotations: `^Int` → sint64, `^Double` → double, `^String`/`^Ptr` → pointer, `^Void` → void, `^Bool` → sint64
- Lazy dlsym: symbol resolution deferred to first call and cached
- Handles allocated in root scope (permanent, survive scope release)


### 5.11 Coroutine Primitives (4)
| Primitive | Arity | Description |
|-----------|-------|-------------|
| `coroutine` | 1 | Create a coroutine from a zero-arg thunk |
| `resume` | 1 | Resume a suspended coroutine, returns yielded or final value |
| `yield` | 1 | Yield a value from inside a coroutine, suspending execution |
| `coroutine?` | 1 | Test if value is a coroutine |

```lisp
(define f (coroutine (lambda () (yield 1) (yield 2) 3)))
(resume f)  ;; => 1
(resume f)  ;; => 2
(resume f)  ;; => 3
```

- Coroutines run on dedicated mmap'd stacks (64KB) with guard pages
- Stack overflow detected via SIGSEGV handler on guard page

### 5.12 Memory

| Primitive | Arity | Description |
|-----------|-------|-------------|
| `unsafe-free!` | 1 | Free heap backing of array/dict/instance/string. Value becomes an error — accessing it after free raises "use after unsafe-free!". No-op on int/nil/other non-heap types. |

---

## 6. Continuation & Effect System (Low-Level)

### 6.1 Delimited Continuations
- `reset`/`shift` based (not call/cc)
- Multi-shot: each invocation of `k` clones the captured stack via `coro_clone`
- `reset` body runs on a dedicated coroutine (mmap'd 64KB stack)
- x86_64 assembly context switching with FPU state isolation
- Stack overflow detection via guard pages + SIGSEGV handler

### 6.2 Effect Handlers
- Installed on prompt frames
- `signal` searches handler stack top-to-bottom
- Handler can resume via `(resolve expr)` or abort by returning a value directly
- Handler result bypasses intermediate computation back to `handle`

### 6.3 What Omni Does NOT Have
- **No call/cc** — uses delimited continuations instead
- **No garbage collection** — uses scope-region memory with deterministic reclamation (arena-per-call + reference counting)
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
| reset/shift | Y | Y |
| handle/signal | Y | Y |
| quasiquote | Y | Y |
| modules | Y | Y |
| dot-bracket `.[i]` | Y | Y |
| path `a.b.c` | Y | Y |

---

## 8. Evaluation Model

### Application
- `(f a b c)` parsed as `E_CALL` with func + args list
- Primitives receive all args at once
- Multi-param closures receive all args at once (strict arity — arity mismatch is an error)
- Binary primitives are the exception: `(+ 3)` returns a `PARTIAL_PRIM` (not a lambda)
- `_` placeholder creates a lambda at parse time: `(f 1 _)` → `(lambda (__p1) (f 1 __p1))`
- Dynamic argument count (no fixed limit on AST; JIT compiles up to 16 args natively)

### Scoping
- Lexical scoping with closure-based environments
- Linked-list environment frames (up to 256 bindings per frame)
- Global environment searched after local

### Memory — Arena-per-Call with RC Escape Hatch
- **Scope regions**: Lexically-scoped bump arenas. Each function call, `let`, and `run()` gets its own `ScopeRegion` — a bump allocator (~3 instructions per allocation) with a linked-list destructor chain
- **Deterministic release**: Scopes are freed at known program points (function return, let exit, run completion). No GC pauses
- **Reference-counted closures**: When a closure escapes its scope (e.g., returned from a function), `copy_to_parent` creates a new Value wrapper sharing the `Closure*` via refcount. The Closure owns a standalone `env_scope` holding its captured environment. Freed when the last reference's destructor runs
- **TCO scope recycling**: At each tail-call bounce, if the current scope has RC=1 (nothing escaped), the scope is swapped for a fresh one — all body temporaries freed per loop iteration (Perceus-style reuse analysis)
- **Root scope**: Permanent scope for `define`'d values, primitives, and type instances. Never released
- **AST allocation**: Expr and Pattern nodes use a separate pool-based region (`root_region`) — permanent, never freed
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

GNU Lightning-based JIT compiler (`src/lisp/jit.c3`) — **sole execution engine**:
- Compiles expressions to native x86_64 machine code
- JIT-native support for all expression types including effects, modules, quasiquote, dispatch
- TCO via trampoline: `_tail` apply variants set bounce fields, `jit_eval()` loop catches bounces
- 1024-entry `Expr*`→`JitFn` compilation cache with sub-expression caching
- Per-eval scope: `run()` wraps each evaluation in a child scope region (freed on return)
- Per-call scope: each closure body evaluation gets its own scope via `jit_eval_in_call_scope`
- JIT GC: destroys all states + clears cache when pool > 75% between top-level evaluations

---

## 11. Lisp-to-C3 Compiler

Transpiler (`src/lisp/compiler.c3`) generates C3 source code:
- Runtime library (`src/lisp/runtime.c3`) provides value representation
- TCO via V_THUNK trampoline pattern
- All expression types compile natively (no interpreter delegation for effects/modules/quasiquote)
- Type definitions and dispatch resolution delegate to interpreter
- Supports: lambda, if, let (incl. ^rec), define, match, begin, and/or, call, reset/shift, handle/signal, quasiquote, modules, literals

### 11b. AOT Compilation

- `./build/main --build input.lisp -o output` — compiles Lisp to C3 to standalone binary
- Generates 5 files: main.c3, continuation.c3, ghost_index.c3, runtime.c3, generated.c3
- AOT binaries link only libc/libm/libdl (no GNU Lightning, no readline)
- All 8 expression types (reset/shift/handle/signal/quasiquote/defmacro/module/import) compile natively
- Runtime bridge (`runtime_bridge.c3`) provides interpreter-to-runtime conversion

---

## 12. Project Tooling

### 12.1 `--init` — Scaffold a New Project

```bash
./build/main --init myproject
```

Creates a project directory with `omni.toml`, `src/main.omni`, `lib/ffi/`, `include/`, and `build/` (containing the generated `project.json` for C3).

### 12.2 `--bind` — Auto-Generate FFI Bindings

```bash
./build/main --bind myproject/
```

Reads `omni.toml`, parses C headers using libclang, and generates typed Omni FFI modules in `lib/ffi/`.

**omni.toml format:**

```toml
[project]
name = "myproject"
version = "0.1.0"

[dependencies.ffi.math]
library = "m"
headers = ["/usr/include/math.h"]
functions = ["sin", "cos", "sqrt"]    # optional filter
```

**Generated `lib/ffi/math.omni`:**

```lisp
(module ffi-math (export cos sin sqrt)
  (define _lib (ffi-open "libm.so"))
  (define (sin (^Double arg0))
    (ffi-call _lib "sin" 'double arg0 'double))
  ;; ...
)
```

**C-to-Omni type mapping:** `int`/`long` → `'int`/`^Int`, `double`/`float` → `'double`/`^Double`, `char*` → `'string`/`^String`, `void*` → `'ptr`/`^Int`.

**Requires:** libclang (optional runtime dependency, only loaded when `--bind` runs).

See `docs/PROJECT_TOOLING.md` for the complete reference.

---

## 13. Limits

| Resource | Limit |
|----------|-------|
| Symbol name length | Dynamic (heap-allocated) |
| Total symbols | 8192 |
| Bindings per env frame | 512 |
| Values | Scope-region bump-allocated (no fixed pool) |
| Environments | Scope-region bump-allocated (no fixed pool) |
| Expressions | Pool-allocated in root region (no fixed pool) |
| Patterns | Pool-allocated in root region (no fixed pool) |
| Match clauses | Dynamic (no fixed limit) |
| Effect handler clauses | Dynamic (no fixed limit) |
| Handler stack depth | 16 |
| Call arguments | Dynamic AST (JIT compiles up to 16 natively) |
| Path segments | 8 |
| Pattern elements | Dynamic (no fixed limit) |
| Macros | 64 |
| Modules | 32 |
| Macro clauses | 8 per macro |
| Module exports | 128 per module |
| Begin expressions | Dynamic (no fixed limit) |
| Lambda params | Dynamic (no fixed limit) |
| String literal (inline) | 63 bytes (Token.text[64] lexer limit) |
| String value | Dynamic (heap-allocated, no limit) |
| Eval depth | 5000 (stack overflow guard) |
| Registered types | 256 |
| Type fields | 16 |
| Method table entries | 64 |
