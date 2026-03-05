# Effects, Continuations, Coroutines, Iterators & Error Handling

**[Back to Index](../OMNI_REFERENCE.md)**

---

## 16. Algebraic Effects

Effects let you intercept, redirect, or suppress side effects without changing
the code that signals them.

### Basics: `signal` and `handle`

```lisp
(handle
  (+ 1 (signal read nil))
  (read x (resolve 41)))
;; => 42
```

1. Body starts evaluating `(+ 1 (signal read nil))`
2. `signal` signals the `read` effect with argument `nil`
3. Handler clause `(read x ...)` matches
4. `x` is bound to `nil`
5. `(resolve 41)` sends `41` back to the body
6. Body finishes: `(+ 1 41)` => `42`

### Resolve (Resume) vs Abort

```lisp
;; Resolve — body continues
(handle (signal double 5)
  (double x (resolve (* x 2))))
;; => 10

;; Abort — body abandoned
(handle (+ 1 (signal bail 42))
  (bail x x))
;; => 42
```

If `resolve` is not called, the handler's return value replaces the entire
`handle` expression.

### Multiple Clauses

```lisp
(handle
  (begin
    (signal log "starting")
    (+ 1 (signal ask nil)))
  (log msg (resolve nil))
  (ask _   (resolve 41)))
;; => 42
```

### Declaring Custom Effects

```lisp
(define [effect] (my/ask (^String prompt)))

;; Now type-checked:
(signal my/ask "name?")    ;; OK
(signal my/ask 42)         ;; ERROR: 42 is not a String
```

Effects don't need to be declared — undeclared effects skip type checking.

### Practical Patterns

**State:**
```lisp
(define (with-state initial thunk)
  (let (state initial)
    (handle (thunk)
      (get-state _ (resolve state))
      (set-state v (set! state v) (resolve nil)))))

(with-state 0
  (lambda ()
    (signal set-state 10)
    (+ (signal get-state nil) 5)))   ;; => 15
```

**Logger:**
```lisp
(define (with-logger thunk)
  (let (logs '())
    (handle (begin (thunk) (reverse logs))
      (log msg (set! logs (cons msg logs)) (resolve nil)))))

(with-logger
  (lambda ()
    (signal log "step 1")
    (signal log "step 2")))
;; => ("step 1" "step 2")
```

**Dependency Injection:**
```lisp
(define (with-mock-db thunk)
  (handle (thunk)
    (db/query sql (resolve '((id 1) (name "test"))))))

(define (get-user) (signal db/query "SELECT * FROM users LIMIT 1"))

(with-mock-db get-user)
;; => ((id 1) (name "test"))
```

---

## 17. Delimited Continuations

### `reset` / `shift`

```lisp
(reset body)
(shift k body)
```

`shift` captures the continuation up to the enclosing `reset` and binds it
to `k`. `k` is a function — `(k value)` resumes with `value`.

```lisp
(reset (+ 1 (shift k (k 10))))
;; k = (lambda (x) (+ 1 x))
;; => 11

;; Multi-shot: k can be called multiple times
(reset (+ 1 (shift k (k (k 10)))))
;; => (+ 1 (+ 1 10)) => 12
```

### Semantics

- Continuations are **multi-shot** — each invocation clones the stack
- The result of `shift`'s body becomes the result of `reset`
- Continuations run on dedicated mmap'd stacks (64KB) with guard pages
- x86_64 assembly context switching with FPU state isolation

---

## 18. Coroutines

```lisp
(define counter
  (coroutine (lambda ()
    (yield 1)
    (yield 2)
    (yield 3))))

(resume counter)    ;; => 1
(resume counter)    ;; => 2
(resume counter)    ;; => 3
(resume counter)    ;; => nil (completed)
```

### API

| Primitive | Arity | Description |
|-----------|-------|-------------|
| `coroutine` | 1 | Create from zero-arg thunk |
| `resume` | 1 | Resume, returns yielded/final value |
| `yield` | 1 | Yield value, suspend execution |
| `coroutine?` | 1 | Type check |

### Generator Pattern

```lisp
(define (fibonacci)
  (coroutine (lambda ()
    (let loop (a 0 b 1)
      (yield a)
      (loop b (+ a b))))))

(define fib (fibonacci))
(resume fib)   ;; => 0
(resume fib)   ;; => 1
(resume fib)   ;; => 1
(resume fib)   ;; => 2
(resume fib)   ;; => 3
```

Coroutines run on dedicated mmap'd stacks (64KB) with guard pages. They are
**not** built on effects — they use direct StackCtx-based context switching.

---

## 19. Iterators

Lazy sequences that compute values on demand.

### Creating Iterators

```lisp
(iterator '(1 2 3))         ;; from list
(iterator [1 2 3])           ;; from array
(iterator {'a 1 'b 2})      ;; from dict (iterates keys)
(range-from 0)                ;; infinite: 0, 1, 2, 3, ...
(repeat 42)                   ;; infinite: 42, 42, 42, ...
(cycle [1 2 3])               ;; infinite: 1,2,3,1,2,3,...
```

### Lazy Combinators

```lisp
(map (+ 1) (iterator '(1 2 3)))         ;; lazy +1 on each
(filter even? (iterator '(1 2 3 4)))    ;; lazy keep evens
(take 5 (range-from 0))                  ;; first 5 naturals
(drop 3 (iterator '(1 2 3 4 5)))        ;; skip first 3
(zip (iterator '(1 2)) (iterator '(a b))) ;; zip two iterators
```

### Consuming Iterators

```lisp
(list (take 5 (range-from 0)))          ;; => (0 1 2 3 4)
(array (take 5 (range-from 0)))         ;; => [0 1 2 3 4]
(foldl + 0 (iterator '(1 2 3 4 5)))     ;; => 15
```

### Pipeline Example

```lisp
;; First 3 even squares starting from 1
(list
  (take 3
    (filter even?
      (map (lambda (x) (* x x))
        (range-from 1)))))
;; => (4 16 36)
```

### Dispatched Collection Ops on Iterators

`map`, `filter`, `take`, `drop`, `zip`, and `foldl` are dispatched.
Passing an iterator keeps lazy semantics:

```lisp
(map (+ 1) (iterator '(1 2 3)))     ;; lazy iterator, not a list
(filter even? (iterator '(1 2 3 4))) ;; lazy iterator
(take 3 (range-from 0))              ;; lazy iterator
```

---

## 20. Error Handling

Errors in Omni flow through the `raise` effect.

### `try` / `assert!`

```lisp
(try
  (lambda (_)
    (assert! (> 1 2) "math is broken")
    "ok")
  (lambda (msg) (string-append "caught: " msg)))
;; => "caught: math is broken"
```

### Direct Handle

```lisp
(handle
  (begin
    (println "before")
    (signal raise "oops")
    (println "after"))     ;; never executes
  (raise msg (string-append "error: " msg)))
;; => "error: oops"
```

### Error Values

```lisp
(error "something went wrong")   ;; create error value
(error-message err)               ;; extract message
```
