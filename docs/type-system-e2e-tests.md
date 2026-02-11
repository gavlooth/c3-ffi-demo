# End-to-End Type System Tests with Memory Regions

These tests demonstrate the complete integration between the Julia-style type
system and the region-based memory management.

---

## Test Suite 1: Basic Types and Regions

### Test 1.1: Simple Struct in Region
```lisp
; Define a 2D point type
(define [type] Point
  (^Float x)
  (^Float y))

; Create a region and allocate points
(with-region r
  (define p1 (Point 1.0 2.0))
  (define p2 (Point 3.0 4.0))

  ; Access fields
  (assert (= p1.x 1.0))
  (assert (= p2.y 4.0))

  ; Points are freed when region exits
  )

; Expected: p1, p2 memory reclaimed
```

### Test 1.2: Nested Regions with Types
```lisp
(define [type] Node
  (^Int value)
  (^(Option Node) next))

(with-region outer
  (define n1 (Node 1 none))

  (with-region inner
    (define n2 (Node 2 (some n1)))  ; n2 references n1
    (assert (= n2.value 2))
    (assert (= n2.next.value 1))
    ; n2 freed here
    )

  ; n1 still valid
  (assert (= n1.value 1))
  )
```

---

## Test Suite 2: Type Hierarchy and Dispatch

### Test 2.1: Abstract Type Hierarchy
```lisp
; Define Julia-style numeric tower
(define [abstract] Number)
(define [abstract] Real Number)
(define [abstract] Integer Real)
(define [abstract] Float Real)

; Concrete types
(define [type] Int64 Integer
  (^i64 value))

(define [type] Float64 Float
  (^f64 value))

; Method dispatches on type hierarchy
(define [method] add (^Number a ^Number b)
  (error "add not defined for these types"))

(define [method] add (^Int64 a ^Int64 b)
  (Int64 (+ a.value b.value)))

(define [method] add (^Float64 a ^Float64 b)
  (Float64 (+ a.value b.value)))

; Test dispatch
(with-region r
  (define i1 (Int64 10))
  (define i2 (Int64 20))
  (define result (add i1 i2))
  (assert (= result.value 30))

  (define f1 (Float64 1.5))
  (define f2 (Float64 2.5))
  (define fresult (add f1 f2))
  (assert (= fresult.value 4.0))
  )
```

### Test 2.2: Subtype Checking
```lisp
(define [method] process (^Real x)
  (print "processing real"))

; Should dispatch correctly
(with-region r
  (define i (Int64 42))
  (define f (Float64 3.14))

  (process i)  ; Int64 <: Integer <: Real
  (process f)  ; Float64 <: Float <: Real
  )
```

---

## Test Suite 3: Parametric Types

### Test 3.1: Simple Parametric Type
```lisp
(define [type] (Box T)
  (^T value))

(with-region r
  (define int-box (Box Int 42))
  (define str-box (Box String "hello"))

  (assert (= int-box.value 42))
  (assert (= str-box.value "hello"))
  )
```

### Test 3.2: Parametric Container with Region
```lisp
(define [type] (List T)
  (^T head)
  (^(Option (List T)) tail))

(define [method] (T) make-list (^T item)
  (List item none))

(define [method] (T) cons (^T item ^(List T) rest)
  (List item (some rest)))

(define [method] (T) length (^(List T) lst)
  (if (none? lst.tail)
      1
      (+ 1 (length lst.tail))))

(with-region r
  (define lst (cons 1 (cons 2 (cons 3 (make-list 4)))))
  (assert (= (length lst) 4))
  (assert (= lst.head 1))
  )
```

### Test 3.3: Constrained Parametric Type
```lisp
(define [type] ^{:where [T Number]}
  (NumericPair T)
  (^T first)
  (^T second))

(define [method] ^{:where [T Number]}
  sum-pair (^(NumericPair T) pair)
  (add pair.first pair.second))

(with-region r
  (define ip ((NumericPair Int64) (Int64 10) (Int64 20)))
  (assert (= (sum-pair ip).value 30))

  ; This should be a compile error:
  ; (define sp ((NumericPair String) "a" "b"))  ; String is not a Number
  )
```

---

## Test Suite 4: Destructors and Cleanup

### Test 4.1: Custom Destructor
```lisp
(define destructor-called (ref false))

(define [type] ^:destructor cleanup-handle
  Handle
  (^Int id))

(define [destructor Handle] (^Handle h)
  (set! destructor-called true)
  (print (format "cleaning up handle {}" h.id)))

(with-region r
  (define h (Handle 42))
  (assert (= h.id 42))
  (assert (= @destructor-called false))
  )

; After region exit
(assert (= @destructor-called true))
```

### Test 4.2: Destructor with Parametric Type
```lisp
(define cleanup-log (ref '()))

(define [type] (Resource T)
  (^T data)
  (^String name))

(define [destructor (T)] (Resource T) (^(Resource T) res)
  (set! cleanup-log (cons res.name @cleanup-log)))

(with-region r
  (define r1 ((Resource Int) 1 "resource-1"))
  (define r2 ((Resource Int) 2 "resource-2"))
  (define r3 ((Resource String) "data" "resource-3"))
  )

; Destructors called in reverse allocation order
(assert (= @cleanup-log '("resource-3" "resource-2" "resource-1")))
```

---

## Test Suite 5: Region-Polymorphic Code

### Test 5.1: Region Parameter
```lisp
(define [method] ^{:where [R Region]}
  alloc-point-in (^R region ^Float x ^Float y)
  (in-region region (Point x y)))

(with-region outer
  (with-region inner
    ; Allocate in outer region from inner scope
    (define p (alloc-point-in outer 1.0 2.0))
    ; p will survive inner region exit
    )

  ; p still valid here
  (assert (= p.x 1.0))
  )
```

### Test 5.2: Arena Pattern
```lisp
(define [type] Arena
  (^Region region)
  (^Int allocation-count))

(define [method] make-arena ()
  (Arena (make-region) 0))

(define [method] (T) arena-alloc (^Arena arena ^T value)
  (set! arena.allocation-count (+ arena.allocation-count 1))
  (in-region arena.region value))

(define [method] arena-reset (^Arena arena)
  (region-clear arena.region)
  (set! arena.allocation-count 0))

(with-region r
  (define arena (make-arena))

  (define p1 (arena-alloc arena (Point 1.0 2.0)))
  (define p2 (arena-alloc arena (Point 3.0 4.0)))
  (assert (= arena.allocation-count 2))

  (arena-reset arena)
  (assert (= arena.allocation-count 0))
  ; p1, p2 now invalid - don't access!

  (define p3 (arena-alloc arena (Point 5.0 6.0)))
  (assert (= arena.allocation-count 1))
  )
```

---

## Test Suite 6: Union Types and Pattern Matching

### Test 6.1: Option Type
```lisp
(define [union] (Option T)
  None
  (Some T))

(define [method] (T) unwrap-or (^(Option T) opt ^T default)
  (match opt
    (None default)
    ((Some x) x)))

(with-region r
  (define maybe-int (Some 42))
  (define nothing None)

  (assert (= (unwrap-or maybe-int 0) 42))
  (assert (= (unwrap-or nothing 0) 0))
  )
```

### Test 6.2: Result Type for Error Handling
```lisp
(define [union] (Result T E)
  (Ok T)
  (Err E))

(define [type] ParseError
  (^String message)
  (^Int position))

(define [method] parse-int (^String s)
  (if (valid-int-string? s)
      (Ok (string->int s))
      (Err (ParseError "invalid integer" 0))))

(with-region r
  (define good (parse-int "42"))
  (define bad (parse-int "hello"))

  (match good
    ((Ok n) (assert (= n 42)))
    ((Err e) (error "unexpected error")))

  (match bad
    ((Ok n) (error "expected error"))
    ((Err e) (assert (= e.message "invalid integer"))))
  )
```

---

## Test Suite 7: Effect Handlers with Types

### Test 7.1: Typed Effects
```lisp
(define [effect] (State T)
  (get ^(Proc T))
  (put ^(Proc T Unit)))

(define [method] (T) run-state (^T initial ^(Proc (State T) A) computation)
  (define state (ref initial))
  (handle (computation)
    ((get k) (k @state))
    ((put v k) (set! state v) (k ()))))

(with-region r
  (define result
    (run-state 0
      (lambda ()
        (define x (perform get))
        (perform put (+ x 10))
        (perform get))))

  (assert (= result 10))
  )
```

### Test 7.2: Region-Bound Effects
```lisp
(define [effect] (Alloc R)
  (alloc ^(Proc (Proc A) A)))  ; allocate in region R

(define [method] ^{:where [R Region]}
  with-allocator (^R region ^(Proc (Alloc R) A) body)
  (handle (body)
    ((alloc thunk k)
     (k (in-region region (thunk))))))

(with-region outer
  (with-region inner
    (define points
      (with-allocator outer
        (lambda ()
          ; These allocations go to outer, not inner
          (list
            (perform alloc (lambda () (Point 1.0 2.0)))
            (perform alloc (lambda () (Point 3.0 4.0)))))))
    ; inner exits here
    )

  ; points still valid - allocated in outer
  (assert (= (length points) 2))
  )
```

---

## Test Suite 8: Complete Integration Example

### Test 8.1: Expression Evaluator with Typed AST
```lisp
; Abstract syntax tree
(define [abstract] Expr)

(define [type] Lit Expr
  (^Int value))

(define [type] Add Expr
  (^Expr left)
  (^Expr right))

(define [type] Mul Expr
  (^Expr left)
  (^Expr right))

(define [type] Let Expr
  (^Symbol name)
  (^Expr init)
  (^Expr body))

(define [type] Var Expr
  (^Symbol name))

; Environment
(define [type] (Env T)
  (^(List (Tuple Symbol T)) bindings))

(define [method] (T) env-lookup (^(Env T) env ^Symbol name)
  (find (lambda (pair) (= (fst pair) name))
        env.bindings))

(define [method] (T) env-extend (^(Env T) env ^Symbol name ^T value)
  (Env (cons (tuple name value) env.bindings)))

; Evaluator with region
(define [method] eval-in-region (^Region r ^Expr expr ^(Env Int) env)
  (in-region r
    (match expr
      ((Lit v) v.value)

      ((Add e)
       (+ (eval-in-region r e.left env)
          (eval-in-region r e.right env)))

      ((Mul e)
       (* (eval-in-region r e.left env)
          (eval-in-region r e.right env)))

      ((Let e)
       (define val (eval-in-region r e.init env))
       (define new-env (env-extend env e.name val))
       (eval-in-region r e.body new-env))

      ((Var e)
       (unwrap (env-lookup env e.name))))))

; Test
(with-region r
  ; (let ((x 10)) (+ x (* x 2)))
  (define expr
    (Let 'x (Lit 10)
      (Add (Var 'x)
           (Mul (Var 'x) (Lit 2)))))

  (define result (eval-in-region r expr (Env '())))
  (assert (= result 30))
  )
```

---

## Performance Benchmarks

### Benchmark 1: Allocation Throughput
```lisp
(define [method] benchmark-alloc (^Int n)
  (with-region r
    (define start (current-time-ns))
    (dotimes (i n)
      (Point (float i) (float i)))
    (define end (current-time-ns))
    (print (format "{} allocations in {} ns ({} ns/alloc)"
                   n (- end start) (/ (- end start) n)))))

(benchmark-alloc 1000000)
; Expected: ~10-50 ns/alloc with region allocator
```

### Benchmark 2: Dispatch Overhead
```lisp
(define [method] benchmark-dispatch (^Int n)
  (with-region r
    (define nums (map (lambda (i) (Int64 i)) (range n)))
    (define start (current-time-ns))
    (fold add (Int64 0) nums)
    (define end (current-time-ns))
    (print (format "{} dispatches in {} ns ({} ns/dispatch)"
                   n (- end start) (/ (- end start) n)))))

(benchmark-dispatch 100000)
; Target: < 100 ns/dispatch with type caching
```
