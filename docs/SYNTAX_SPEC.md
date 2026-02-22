# Omni Lisp Syntax Specification

**Updated:** 2026-02-20

---

## 1. Lexical Elements

### 1.1 Token Types

| Token | Pattern | Description |
|-------|---------|-------------|
| T_EOF | End of file | Reached end of input |
| T_LPAREN | `(` | Left parenthesis for list forms |
| T_RPAREN | `)` | Right parenthesis for list forms |
| T_LBRACKET | `[` | Left bracket for array literals, patterns, attributes |
| T_RBRACKET | `]` | Right bracket for array literals, patterns, attributes |
| T_LBRACE | `{` | Left brace for dict literals, metadata dictionaries |
| T_RBRACE | `}` | Right brace for dict literals, metadata dictionaries |
| T_DOT_BRACKET | `.[` | Path index access (e.g., `arr.[0]`) |
| T_QUOTE | `'` | Quote shorthand |
| T_BACKQUOTE | `` ` `` | Quasiquote shorthand |
| T_COMMA | `,` | Unquote shorthand |
| T_COMMA_AT | `,@` | Unquote-splicing shorthand |
| T_INT | `-?[0-9]+` | Integer literals |
| T_FLOAT | `-?[0-9]+\.[0-9]+` | Floating-point literals |
| T_STRING | `"..."` | String literals with escapes: `\n`, `\t`, `\\`, `\"` |
| T_SYMBOL | `[a-zA-Z0-9_\-+*/=<>!?:@#$%&\|^~]+` | Identifiers |
| T_PATH | `segment.segment[.segment]*` | Dot-separated paths |
| T_UNDERSCORE | `_` | Wildcard (not a symbol) |
| T_DOTDOT | `..` | Rest/spread in patterns and variadic params |
| T_ERROR | Any unrecognized | Invalid token |

### 1.2 Whitespace and Comments

- **Whitespace:** spaces, tabs, newlines skipped
- **Comments:** `;` to end of line

---

## 2. Expression Types (AST)

| Tag | Syntax | Description |
|-----|--------|-------------|
| E_LIT | `42`, `3.14`, `"str"` | Literal values |
| E_VAR | `name` | Variable lookup |
| E_LAMBDA | `(lambda (p) body)` | Function definition |
| E_APP | `(f arg)` | Function application |
| E_CALL | `(f a b c)` | Multi-arg call (3+ args) |
| E_IF | `(if t a b)` | Conditional |
| E_LET | `(let ((n v)) body)` | Local binding |
| E_DEFINE | `(define n v)` | Global definition |
| E_SET | `(set! n v)` | Variable mutation |
| E_BEGIN | `(begin e1 e2 ...)` | Sequencing |
| E_AND | `(and a b)` | Short-circuit and |
| E_OR | `(or a b)` | Short-circuit or |
| E_QUOTE | `'datum` | Prevent evaluation |
| E_QUASIQUOTE | `` `expr `` | Template with unquote |
| E_UNQUOTE | `,expr` | Unquote inside quasiquote |
| E_UNQUOTE_SPLICING | `,@expr` | Splice into quasiquote |
| E_MATCH | `(match expr clauses...)` | Pattern matching |
| E_RESET | `(reset body)` | Delimited continuation prompt |
| E_SHIFT | `(shift k body)` | Capture continuation |
| E_PERFORM | `(perform tag arg)` | Perform algebraic effect |
| E_HANDLE | `(handle body clauses...)` | Handle algebraic effects |
| E_DEFMACRO | `(define [macro] ...)` | Pattern macro definition |
| E_MODULE | `(module name ...)` | Module definition |
| E_IMPORT | `(import name)` | Module import |
| E_DEFTYPE | `(define [type] ...)` | Struct type definition |
| E_DEFABSTRACT | `(define [abstract] ...)` | Abstract type definition |
| E_DEFUNION | `(define [union] ...)` | Union type definition |
| E_DEFALIAS | `(define [alias] ...)` | Type alias definition |
| E_INDEX | `arr.[i]` | Postfix indexing |
| E_PATH | `a.b.c` | Dot-path field access |

---

## 3. Special Forms

### 3.1 `lambda` - Function Definition

```lisp
;; Single parameter
(lambda (x) body)

;; Multi-parameter (auto-curried by parser)
(lambda (x y z) body)
;; desugars to: (lambda (x) (lambda (y) (lambda (z) body)))

;; Zero-argument
(lambda () body)

;; Variadic
(lambda (x .. rest) body)

;; Typed parameters (no currying)
(lambda ((^Int x) (^String y)) body)
```

### 3.2 `if` - Conditional

```lisp
(if test then-expr else-expr)
```

Three branches required (no two-branch form).

### 3.3 `let` - Local Binding

```lisp
;; Simple let
(let ((name init)) body)

;; Recursive let
(let ^rec ((name init)) body)

;; Named let (desugars to let ^rec)
(let loop ((x 0) (acc nil))
  (if (= x 10) acc (loop (+ x 1) (cons x acc))))
```

### 3.4 `define` - Global Definition

```lisp
;; Simple define
(define name value)

;; Shorthand function define
(define (f x y) body)
;; desugars to: (define f (lambda (x y) body))

;; Typed function define (multiple dispatch)
(define (f (^Int x)) "integer")
(define (f (^String x)) "string")
(define (f x) "other")  ;; fallback

;; Bracket attributes
(define [macro] name (pattern template))
(define [type] Name (^Type field1) (^Type field2))
(define [type] (Child Parent) (^Type field))
(define [abstract] Name)
(define [abstract] (Child Parent))
(define [union] (Name T) Variant1 (Variant2 T))
(define [alias] Name TargetType)
```

### 3.5 `set!` - Variable Mutation

```lisp
(set! name value)
(set! instance.field value)      ;; struct field mutation
(set! instance.nested.field value) ;; nested mutation
(set! pair.car value)            ;; cons cell car mutation
(set! pair.cdr value)            ;; cons cell cdr mutation
```

### 3.6 `begin` - Sequencing

```lisp
(begin expr1 expr2 ... exprN)  ;; returns last value, TCO on last
```

### 3.7 `quote` / `quasiquote`

```lisp
(quote datum)        ;; or 'datum
`(a ,(+ 1 2) ,@(list 3 4))  ;; => (a 3 3 4)
```

### 3.8 `match` - Pattern Matching

```lisp
(match expr
  (pattern1 result1)
  (pattern2 result2)
  (_ default))
```

Patterns: literals, variables, wildcards `_`, sequences `[a b .. rest]`, quoted `'sym`, constructors `(Some x)`.

### 3.9 `reset` / `shift` - Delimited Continuations

```lisp
(reset body)
(shift k body)
(reset (+ 1 (shift k (k (k 10)))))  ;; => 12
```

### 3.10 `perform` / `handle` - Algebraic Effects

```lisp
(perform effect-tag argument)
(handle body
  ((tag1 k arg) handler1)
  ((tag2 k arg) handler2))
```

I/O operations go through effects: `print`, `println`, `display`, `read-file`, etc. use `io/print`, `io/println`, etc. effect tags. When no handler is installed, a fast path calls the raw primitives directly.

### 3.11 `module` / `import`

```lisp
(module name (export sym1 sym2 ...)
  body...)
(import name)
```

Modules are cached; circular imports are detected.

### 3.12 Collection Literals

```lisp
;; Array literal — desugars to (array ...)
[1 2 3]                ;; => mutable array with elements 1, 2, 3
[]                     ;; => empty array

;; Dict literal — desugars to (dict ...)
{'a 1 'b 2}            ;; => mutable dict with keys a, b
{}                     ;; => empty dict

;; Must have even number of elements in dict literal
;; {'a 1 'b}           ;; ERROR: odd count
```

---

## 4. Type System

### 4.1 Type Annotations

```lisp
^Int              ;; simple type
^(List Int)       ;; compound type
^(Val 42)         ;; value-level type (dispatch on literal)
^{'T Number}      ;; metadata dictionary
```

### 4.2 Type Definitions

```lisp
(define [type] Point (^Int x) (^Int y))
(define [abstract] Shape)
(define [type] (Circle Shape) (^Int radius))
(define [union] (Option T) None (Some T))
(define [alias] Num Int)
```

### 4.3 Multiple Dispatch

Typed defines create method tables. Best match wins:
- Val literal match: score 1000
- Exact type match: score 100
- Subtype match: score 10
- Any type (untyped param): score 1

---

## 5. Path and Index Notation

```lisp
point.x           ;; struct field access
line.start.y      ;; nested field access
pair.car           ;; cons cell car access
pair.cdr           ;; cons cell cdr access
arr.[0]           ;; list/array index
dict.['key]       ;; dict key lookup
matrix.[i].[j]    ;; chained indexing
```

---

## 6. Truthiness

- **Falsy:** `nil`, `false`
- **Truthy:** Everything else (including `0`, `""`, `'()`)

---

## 7. Currying

All functions are curried. Multi-argument calls desugar:

```lisp
(f a b c)  =>  (((f a) b) c)
```

Exception: typed multi-param lambdas are NOT curried (preserved for dispatch).

Binary primitives return partial application when given one argument.

---

## 8. Grammar (EBNF)

```ebnf
program     = { expr } ;
expr        = literal | symbol | path | quoted | quasiquoted
            | list | array_lit | dict_lit | indexed ;

literal     = integer | float | string ;
integer     = [ "-" ] digit { digit } ;
float       = [ "-" ] digit { digit } "." digit { digit } ;
string      = '"' { char | escape } '"' ;
symbol      = symbol_char { symbol_char } ;
path        = symbol "." symbol { "." symbol } ;

quoted      = "'" datum ;
quasiquoted = "`" datum ;
list        = "(" { expr } ")" ;
array_lit   = "[" { expr } "]" ;           (* desugars to (array ...) *)
dict_lit    = "{" { expr expr } "}" ;      (* desugars to (dict ...), must be even *)
indexed     = expr ".[" expr "]" ;

datum       = literal | symbol | "(" { datum } ")" | "'" datum ;

symbol_char = letter | digit | "_" | "-" | "+" | "*" | "/"
            | "=" | "<" | ">" | "!" | "?" | ":" | "." | "@"
            | "#" | "$" | "%" | "&" | "|" | "^" | "~" ;
```

---

## 9. Limits

| Resource | Limit |
|----------|-------|
| Symbol/string length | 4095 chars |
| Max symbols | 4096 |
| Max values per region | 4096 |
| Max bindings per env frame | 512 |
| Effect clauses per handle | 8 |
| Path segments | 8 |
| Handler stack depth | 16 |
| Match clauses | 16 |
| Pattern elements | 16 |
| Type fields | 16 |
| Method table entries | 32 |
| Registered types | 128 |

---

*Omni Lisp — A Lisp with delimited continuations, algebraic effects, auto-curried lambdas, and multiple dispatch*
