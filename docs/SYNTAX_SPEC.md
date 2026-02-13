# Pika Lisp Syntax Specification

**Extracted from source code:** 2026-02-11

---

## 1. Lexical Elements

### 1.1 Token Types

| Token | Pattern | Description |
|-------|---------|-------------|
| T_EOF | End of file | Reached end of input |
| T_LPAREN | `(` | Left parenthesis for list forms |
| T_RPAREN | `)` | Right parenthesis for list forms |
| T_LBRACKET | `[` | Left bracket for array literals |
| T_RBRACKET | `]` | Right bracket for array literals |
| T_DOT_BRACKET | `.[` | Path index access (e.g., `arr.[0]`) |
| T_QUOTE | `'` | Quote shorthand |
| T_INT | `-?[0-9]+` | Integer literals |
| T_STRING | `"..."` | String literals with escapes: `\n`, `\t`, `\\`, `\"` |
| T_SYMBOL | `[a-zA-Z0-9_\-+*/=<>!?:@#$%&\|^~]+` | Identifiers |
| T_PATH | `segment.segment[.segment]*` | Dot-separated paths |
| T_ERROR | Any unrecognized | Invalid token |

### 1.2 Whitespace and Comments

- **Whitespace:** spaces, tabs, newlines skipped
- **Comments:** `;` to end of line

```lisp
; This is a comment
(+ 1 2)  ; inline comment
```

---

## 2. Expression Types (AST)

| Tag | Syntax | AST Fields |
|-----|--------|------------|
| E_LIT | `42`, `"str"` | `value: Value*` |
| E_VAR | `name` | `name: SymbolId` |
| E_LAMBDA | `(lambda (p) body)` | `param: SymbolId, body: Expr*` |
| E_APP | `(f arg)` | `func: Expr*, arg: Expr*` |
| E_IF | `(if t a b)` | `test, then_branch, else_branch: Expr*` |
| E_LET | `(let ((n v)) body)` | `name: SymbolId, init: Expr*, body: Expr*` |
| E_DEFINE | `(define n v)` | `name: SymbolId, value: Expr*` |
| E_QUOTE | `(quote d)` / `'d` | `datum: Value*` |
| E_RESET | `(reset body)` | `body: Expr*` |
| E_SHIFT | `(shift k body)` | `k_name: SymbolId, body: Expr*` |
| E_PERFORM | `(perform t a)` | `tag: SymbolId, arg: Expr*` |
| E_HANDLE | `(handle b clauses)` | `body: Expr*, clauses[], clause_count` |
| E_INDEX | `arr.[i]` | `collection: Expr*, index: Expr*` |
| E_PATH | `a.b.c` | `segments[]: SymbolId, segment_count` |

---

## 3. Special Forms

### 3.1 `lambda` - Function Definition

```lisp
(lambda (param) body)
```

Single parameter only. Multi-arg uses currying:
```lisp
(lambda (x) (lambda (y) (+ x y)))
```

### 3.2 `if` - Conditional

```lisp
(if test then-expr else-expr)
```

Three branches required (no two-branch form).

### 3.3 `let` - Local Binding

```lisp
(let ((name init)) body)
```

Single binding per `let`. Nest for multiple:
```lisp
(let ((x 1)) (let ((y 2)) (+ x y)))
```

### 3.4 `define` - Global Definition

```lisp
(define name value)
```

### 3.5 `quote` - Prevent Evaluation

```lisp
(quote datum)
'datum           ; shorthand
```

### 3.6 `reset` / `shift` - Delimited Continuations

```lisp
(reset body)
(shift k body)   ; k bound to continuation
```

Example:
```lisp
(reset (+ 1 (shift k (k (k 10)))))  ; => 12
```

### 3.7 `perform` / `handle` - Algebraic Effects

```lisp
(perform effect-tag argument)

(handle body
  ((tag1 k arg) handler1)
  ((tag2 k arg) handler2))
```

Example:
```lisp
(handle
  (+ 1 (perform read nil))
  ((read k x) (k 41)))  ; => 42
```

---

## 4. Path Index Notation

Postfix `.[expr]` for indexing:

```lisp
arr.[0]           ; list/array index
arr.[i]           ; variable index
matrix.[i].[j]    ; chained indexing
dict.['key]       ; quoted symbol key
```

---

## 5. Primitives

### 5.1 Constants

| Name | Description |
|------|-------------|
| `true` | True value |
| `false` | Bound to nil |
| `nil` | Empty/false |

### 5.2 Arithmetic (Arity 2)

| Name | Description |
|------|-------------|
| `+` | Addition |
| `-` | Subtraction |
| `*` | Multiplication |
| `/` | Integer division |
| `%` | Modulo |

### 5.3 Comparison (Arity 2)

| Name | Description |
|------|-------------|
| `=` | Equality |
| `<` | Less than |
| `>` | Greater than |
| `<=` | Less or equal |
| `>=` | Greater or equal |

### 5.4 List Operations

| Name | Arity | Description |
|------|-------|-------------|
| `cons` | 2 | Construct pair |
| `car` | 1 | First element |
| `cdr` | 1 | Rest of list |
| `null?` | 1 | Test if nil |
| `pair?` | 1 | Test if cons |
| `list` | variadic | Create list |

### 5.5 Boolean

| Name | Arity | Description |
|------|-------|-------------|
| `not` | 1 | Logical negation |

### 5.6 I/O

| Name | Arity | Description |
|------|-------|-------------|
| `print` | 1 | Print (no newline) |
| `println` | 1 | Print with newline |
| `newline` | 0 | Print newline |

### 5.7 String Operations

| Name | Arity | Description |
|------|-------|-------------|
| `string-append` | variadic | Concatenate strings |
| `string-join` | 2 | Join list with separator |
| `substring` | 3 | Extract substring (start, end) |
| `string-split` | 2 | Split by delimiter |
| `string-length` | 1 | String length |
| `string->list` | 1 | String to char list |
| `list->string` | 1 | Char list to string |
| `string-upcase` | 1 | To uppercase |
| `string-downcase` | 1 | To lowercase |
| `string-trim` | 1 | Remove whitespace |
| `string?` | 1 | Type predicate |

### 5.8 File I/O

| Name | Arity | Description |
|------|-------|-------------|
| `read-file` | 1 | Read file as string |
| `write-file` | 2 | Write string to file |
| `file-exists?` | 1 | Check file exists |
| `read-lines` | 1 | Read as line list |

---

## 6. Grammar (EBNF)

```ebnf
program     = { expr } ;
expr        = literal | symbol | path | quoted | list | indexed ;

literal     = integer | string ;
integer     = [ "-" ] digit { digit } ;
string      = '"' { char | escape } '"' ;
symbol      = symbol_char { symbol_char } ;
path        = symbol "." symbol { "." symbol } ;

quoted      = "'" datum ;
list        = "(" { expr } ")" ;
indexed     = expr ".[" expr "]" ;

datum       = literal | symbol | "(" { datum } ")" | "'" datum ;

symbol_char = letter | digit | "_" | "-" | "+" | "*" | "/"
            | "=" | "<" | ">" | "!" | "?" | ":" | "@" | "#"
            | "$" | "%" | "&" | "|" | "^" | "~" ;
```

---

## 7. Truthiness

- **Falsy:** `nil`, `false`, `0`
- **Truthy:** Everything else

---

## 8. Currying

All functions are curried. Multi-argument calls desugar:

```lisp
(f a b c)  =>  (((f a) b) c)
```

Binary primitives return partial application when given one argument.

---

## 9. Limits

| Resource | Limit |
|----------|-------|
| Symbol/string length | 64 chars |
| Max symbols | 256 |
| Max values | 4096 |
| Effect clauses per handle | 8 |
| Path segments | 8 |

---

*Pika Lisp - A minimal Lisp with delimited continuations and algebraic effects*
