# Purple Quick Reference

## Data Types
```scheme
42              ; integer
3.14            ; float
'foo            ; symbol
#\a #\newline   ; character
'(1 2 3)        ; list
nil             ; nil/false
t               ; true
```

## Special Forms
```scheme
(lambda (x) body)              ; function
(lambda self (x) body)         ; recursive function
(let ((x val)) body)           ; local binding
(letrec ((f fn)) body)         ; recursive binding
(if cond then else)            ; conditional
(and e1 e2 ...)                ; short-circuit and
(or e1 e2 ...)                 ; short-circuit or
(do e1 e2 ... en)              ; sequence, return last
(quote x) / 'x                 ; quote
(quasiquote x) / `x            ; quasiquote
(unquote x) / ,x               ; unquote in quasiquote
(unquote-splicing x) / ,@x     ; splice in quasiquote
```

## Pattern Matching
```scheme
(match val
  (_              result)      ; wildcard
  (x              result)      ; variable binding
  ((42)           result)      ; literal
  ((cons a b)     result)      ; constructor
  ((or p1 p2)     result)      ; or-pattern
  ((x @ pat)      result)      ; as-pattern
  ((n :when (> n 0)) result))  ; guard
```

## Primitives
```scheme
; Arithmetic
(+ - * / %)

; Comparison
(= < > <= >= not)

; Lists
(cons car cdr null? list length append reverse)

; Higher-order
(map filter fold foldl apply compose flip)

; Introspection
(gensym sym-eq? eval trace)
```

## Staging
```scheme
(lift val)           ; value → code
(run code)           ; execute code (JIT)
(EM expr)            ; meta-level evaluation
(shift n expr)       ; go up n levels
(meta-level)         ; current tower level
(clambda (x) body)   ; compile lambda
```

## Handlers
```scheme
(get-meta 'name)                    ; get handler
(set-meta! 'name fn)                ; set handler
(with-handlers ((name fn)) body)    ; scoped handlers
(default-handler 'name arg)         ; call default

; Handler names: lit var lam app if lft run em clam
```

## Macros
```scheme
(defmacro name (params) body scope)
(mcall macro-name args...)
(macroexpand expr)
```

## FFI
```scheme
(ffi "func" args...)
(ffi-declare "ret" "name" "arg1" ...)
```

## Errors
```scheme
(error msg)
(try expr handler)
(assert cond)
(assert cond msg)
```

## CLI
```bash
purple                    # REPL
purple -e '(+ 1 2)'       # eval expression
purple file.purple        # run file
purple -c file.purple     # compile to C
purple -c -o out.c file   # compile to file
purple -v file            # verbose mode
```

## REPL Commands
```
help      - show help
quit      - exit
compile   - toggle compile mode
macros    - list macros
runtime   - print C runtime
```
