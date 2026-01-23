;; test_pipe_operator.lisp - Test pipe operator (|>) functionality
;;
;; The pipe operator (|>) allows left-to-right function composition
;; (x |> f) is equivalent to (f x)
;; Run with: ./omni tests/test_pipe_operator.lisp

;; ============================================================
;; Test Framework
;; ============================================================

(define test-count 0)
(define pass-count 0)
(define fail-count 0)

(define test-eq [name] [expected] [actual]
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
;; Test 1: Basic pipe with increment
;; Tests that pipe correctly passes value to a lambda
;; ============================================================
(define inc [x] (+ x 1))
(test-eq "pipe basic inc" 6 (5 |> inc))

;; ============================================================
;; Test 2: Pipe chain - multiple operations
;; Tests that multiple pipe operations compose correctly
;; (5 |> inc |> inc) should be ((inc 5) |> inc) = 6 |> inc = 7
;; ============================================================

(test-eq "pipe chain inc" 7 (5 |> inc |> inc))

;; ============================================================
;; Test 3: Pipe with different operations
;; Tests that pipe works with different function types
;; ============================================================
(define square [x] (* x x))
(test-eq "pipe with square" 25 (5 |> square))

;; ============================================================
;; Test 4: Complex pipe chain
;; Tests a chain of different operations:
;; 5 |> inc    = 6
;; 6 |> square = 36
;; 36 |> inc   = 37
;; ============================================================

(test-eq "pipe chain complex" 37 (5 |> inc |> square |> inc))

;; ============================================================
;; Test 5: Pipe with multiplication
;; Tests arithmetic operations through pipe
;; ============================================================
(define double [x] (* x 2))
(test-eq "pipe with double" 10 (5 |> double))

;; ============================================================
;; Test 6: Pipe chain mixing operations
;; Tests that different operation types compose correctly
;; 5 |> inc    = 6
;; 6 |> double = 12
;; 12 |> square = 144
;; ============================================================

(test-eq "pipe mixed ops" 144 (5 |> inc |> double |> square))

;; ============================================================
;; Test 7: Pipe with negative numbers
;; Tests edge case with negative input
;; ============================================================

(test-eq "pipe negative input" -4 (-5 |> inc))

;; ============================================================
;; Test 8: Pipe with zero
;; Tests edge case with zero input
;; ============================================================

(test-eq "pipe zero input" 1 (0 |> inc))

;; ============================================================
;; Test 9: Pipe identity-like operation
;; Tests that pipe doesn't modify value when function returns it
;; ============================================================

(define identity [x] x)
(test-eq "pipe identity" 42 (42 |> identity))

;; ============================================================
;; Test 10: Lambda in pipe
;; Tests that inline lambdas work with pipe
;; ============================================================

(test-eq "pipe lambda" 11 (10 |> (lambda [x] (+ x 1))))

;; ============================================================
;; Test 11: Nested lambda in pipe
;; Tests more complex lambda expressions
;; ============================================================

(test-eq "pipe nested lambda" 20 (10 |> (lambda [x] (* 2 x))))

;; ============================================================
;; Results
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
