;; test_let_seq.lisp - Tests for Sequential Let Bindings (let ^:seq)
;;
;; Tests the ^:seq metadata for let bindings, which allows each
;; binding to see previous bindings (like Scheme's let*).
;;
;; Run with: ./omni tests/test_let_seq.lisp

;; ============================================================
;; Test Framework
;; ============================================================

(define test-count 0)
(define pass-count 0)
(define fail-count 0)

(define test-num [name] [expected] [actual]
  (set! test-count (+ test-count 1))
  (if (= expected actual)
      (do
        (set! pass-count (+ pass-count 1))
        (print "PASS:" name))
      (do
        (set! fail-count (+ fail-count 1))
        (print "FAIL:" name)
        (print "  Expected:" expected)
        (print "  Got:" actual))))

(define test-bool [name] [expected] [actual]
  (set! test-count (+ test-count 1))
  (if (= expected actual)
      (do
        (set! pass-count (+ pass-count 1))
        (print "PASS:" name))
      (do
        (set! fail-count (+ fail-count 1))
        (print "FAIL:" name)
        (print "  Expected:" expected)
        (print "  Got:" actual))))

(define test-string [name] [expected] [actual]
  (set! test-count (+ test-count 1))
  (if (= expected actual)
      (do
        (set! pass-count (+ pass-count 1))
        (print "PASS:" name))
      (do
        (set! fail-count (+ fail-count 1))
        (print "FAIL:" name)
        (print "  Expected:" expected)
        (print "  Got:" actual))))

;; ============================================================
;; Test 1: Basic sequential binding
;; ============================================================

(print "")
(print "=== Test 1: Basic Sequential Binding ===")

(define test1
  (let ^:seq [x 1] [y (+ x 10)]
    y))

(test-num "sequential binding visibility" 11 test1)

;; ============================================================
;; Test 2: Chain of dependencies
;; ============================================================

(print "")
(print "=== Test 2: Chain of Dependencies ===")

(define test2
  (let ^:seq 
    [a 5] 
    [b (* a 2)]      ; b = 10
    [c (+ b 3)]      ; c = 13
    c))

(test-num "chain of three bindings" 13 test2)

;; ============================================================
;; Test 3: Shadowing in sequential bindings
;; ============================================================

(print "")
(print "=== Test 3: Variable Shadowing ===")

(define test3
  (let ^:seq 
    [x 10] 
    [x (* x 2)]      ; shadow outer x
    x))

(test-num "shadowing in sequential let" 20 test3)

;; ============================================================
;; Test 4: Mixed with untyped and typed bindings
;; ============================================================

(print "")
(print "=== Test 4: Mixed Typed/Untyped Bindings ===")

(define test4
  (let ^:seq 
    [x 100]
    [y {Int} 50]
    [z (+ x y)]
    z))

(test-num "mixed typed/untyped sequential bindings" 150 test4)

;; ============================================================
;; Test 5: Sequential binding with destructuring
;; ============================================================

(print "")
(print "=== Test 5: Sequential Binding with Destructuring ===")

(define test5
  (let ^:seq
    [nums [1 2 3]]
    [[first second third] nums]
    [sum (+ first (+ second third))]
    sum))

(test-num "sequential binding with array destructuring" 6 test5)

;; ============================================================
;; Test 6: Arithmetic operations in sequence
;; ============================================================

(print "")
(print "=== Test 6: Arithmetic Operations in Sequence ===")

(define test6
  (let ^:seq
    [base 2]
    [square (* base base)]      ; 4
    [cube (* square base)]      ; 8
    cube))

(test-num "sequential arithmetic (2^3)" 8 test6)

;; ============================================================
;; Test 7: String concatenation in sequence
;; ============================================================

(print "")
(print "=== Test 7: String Concatenation ===")

(define test7
  (let ^:seq
    [first "Hello"]
    [second " "]
    [third "World"]
    (string-append first second third)))

(test-string "sequential string concatenation" "Hello World" test7)

;; ============================================================
;; Test 8: Nested sequential lets
;; ============================================================

(print "")
(print "=== Test 8: Nested Sequential Lets ===")

(define test8
  (let ^:seq [outer 10]
    (let ^:seq [inner (+ outer 5)]
      (+ outer inner))))

(test-num "nested sequential lets" 25 test8)

;; ============================================================
;; Test 9: Conditional logic in sequential bindings
;; ============================================================

(print "")
(print "=== Test 9: Conditional Logic ===")

(define test9
  (let ^:seq
    [value 15]
    [doubled (* value 2)]
    (if (> doubled 20) "large" "small")))

(test-string "conditional after sequential binding" "large" test9)

;; ============================================================
;; Test 10: Comparison of sequential vs non-sequential
;; ============================================================

(print "")
(print "=== Test 10: Sequential vs Non-Sequential ===")

;; Non-sequential: x and y are bound simultaneously
(define non-seq-result
  (let [x 5] [y x]
    y))  ; This might fail or have undefined behavior since x is being bound

;; Sequential: y can see x
(define seq-result
  (let ^:seq [x 5] [y x]
    y))  ; y = 5

(test-num "sequential binding allows reference to previous binding" 5 seq-result)

;; ============================================================
;; Test 11: Empty sequential let
;; ============================================================

(print "")
(print "=== Test 11: Edge Cases ===")

(define test11
  (let ^:seq
    x))  ; Just return x from outer scope

;; Should return outer x if defined
(test-string "empty sequential let preserves outer scope" x test11)

;; ============================================================
;; Test 12: Multiple operations in one sequential binding
;; ============================================================

(print "")
(print "=== Test 12: Complex Sequential Computation ===")

(define test12
  (let ^:seq
    [n 10]
    [sq (* n n)]         ; 100
    [double (* sq 2)]     ; 200
    [minus (- double 50)]   ; 150
    minus))

(test-num "complex sequential computation" 150 test12)

;; ============================================================
;; Test Results
;; ============================================================

(print "")
(print "=== Test Results ===")
(print "Total:" test-count)
(print "Passed:" pass-count)
(print "Failed:" fail-count)

(if (= fail-count 0)
    (print "ALL TESTS PASSED!")
    (print "SOME TESTS FAILED"))

;; Return count of failures (0 = success)
fail-count
