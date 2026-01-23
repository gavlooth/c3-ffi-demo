;; test_rounding.lisp - Tests for rounding functions (floor, ceil, round, trunc)
;;
;; These functions are fundamental for mathematical computations,
;; scientific computing, graphics, and algorithm design.
;; Run with: ./omni tests/test_rounding.lisp

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

(define test-float [name] [expected] [actual] [epsilon]
  (set! test-count (+ test-count 1))
  (let ([diff (- actual expected)])
    (if (< (abs diff) epsilon)
        (do
          (set! pass-count (+ pass-count 1))
          (print "PASS:" name))
        (do
          (set! fail-count (+ fail-count 1))
          (print "FAIL:" name)
          (print "  Expected:" expected)
          (print "  Got:" actual)
          (print "  Diff:" diff)))))

;; ============================================================
;; Test 1: floor function - Basic functionality
;; ============================================================

(print "")
(print "=== Test 1: floor Function ===")

(test-float "floor(3.7) = 3"
  3.0
  (floor 3.7)
  0.0001)

(test-float "floor(3.2) = 3"
  3.0
  (floor 3.2)
  0.0001)

(test-float "floor(-3.7) = -4"
  -4.0
  (floor -3.7)
  0.0001)

(test-float "floor(-3.2) = -4"
  -4.0
  (floor -3.2)
  0.0001)

(test-eq "floor(5) = 5 (integer input)"
  5
  (floor 5))

(test-eq "floor(-5) = -5 (negative integer)"
  -5
  (floor -5))

(test-float "floor(0.5) = 0"
  0.0
  (floor 0.5)
  0.0001)

(test-float "floor(-0.5) = -1"
  -1.0
  (floor -0.5)
  0.0001)

;; ============================================================
;; Test 2: floor function - Edge cases and boundaries
;; ============================================================

(print "")
(print "=== Test 2: floor Edge Cases ===")

(test-eq "floor(0) = 0 (zero)"
  0
  (floor 0))

(test-float "floor(-0.0) = 0 (negative zero)"
  0.0
  (floor -0.0)
  0.0001)

(test-float "floor(1.0) = 1 (exact float)"
  1.0
  (floor 1.0)
  0.0001)

(test-float "floor(-1.0) = -1 (negative exact float)"
  -1.0
  (floor -1.0)
  0.0001)

(test-float "floor(9999999.9) = 9999999 (large positive)"
  9999999.0
  (floor 9999999.9)
  0.0001)

(test-float "floor(-9999999.9) = -10000000 (large negative)"
  -10000000.0
  (floor -9999999.9)
  0.0001)

(test-float "floor(0.0001) = 0 (very small positive)"
  0.0
  (floor 0.0001)
  0.0001)

(test-float "floor(-0.0001) = -1 (very small negative)"
  -1.0
  (floor -0.0001)
  0.0001)

;; ============================================================
;; Test 3: ceil function - Basic functionality
;; ============================================================

(print "")
(print "=== Test 3: ceil Function ===")

(test-float "ceil(3.2) = 4"
  4.0
  (ceil 3.2)
  0.0001)

(test-float "ceil(3.7) = 4"
  4.0
  (ceil 3.7)
  0.0001)

(test-float "ceil(-3.2) = -3"
  -3.0
  (ceil -3.2)
  0.0001)

(test-float "ceil(-3.7) = -3"
  -3.0
  (ceil -3.7)
  0.0001)

(test-eq "ceil(5) = 5 (integer input)"
  5
  (ceil 5))

(test-eq "ceil(-5) = -5 (negative integer)"
  -5
  (ceil -5))

(test-float "ceil(0.5) = 1"
  1.0
  (ceil 0.5)
  0.0001)

(test-float "ceil(-0.5) = 0"
  0.0
  (ceil -0.5)
  0.0001)

;; ============================================================
;; Test 4: ceil function - Edge cases and boundaries
;; ============================================================

(print "")
(print "=== Test 4: ceil Edge Cases ===")

(test-eq "ceil(0) = 0 (zero)"
  0
  (ceil 0))

(test-float "ceil(-0.0) = 0 (negative zero)"
  0.0
  (ceil -0.0)
  0.0001)

(test-float "ceil(1.0) = 1 (exact float)"
  1.0
  (ceil 1.0)
  0.0001)

(test-float "ceil(-1.0) = -1 (negative exact float)"
  -1.0
  (ceil -1.0)
  0.0001)

(test-float "ceil(9999999.1) = 10000000 (large positive)"
  10000000.0
  (ceil 9999999.1)
  0.0001)

(test-float "ceil(-9999999.1) = -9999999 (large negative)"
  -9999999.0
  (ceil -9999999.1)
  0.0001)

(test-float "ceil(0.9999) = 1 (close to 1)"
  1.0
  (ceil 0.9999)
  0.0001)

(test-float "ceil(-0.9999) = 0 (close to 0 negative)"
  0.0
  (ceil -0.9999)
  0.0001)

;; ============================================================
;; Test 5: round function - Basic functionality (banker's rounding)
;; ============================================================

(print "")
(print "=== Test 5: round Function ===")

(test-float "round(3.4) = 3"
  3.0
  (round 3.4)
  0.0001)

(test-float "round(3.5) = 4"
  4.0
  (round 3.5)
  0.0001)

(test-float "round(3.6) = 4"
  4.0
  (round 3.6)
  0.0001)

(test-float "round(-3.4) = -3"
  -3.0
  (round -3.4)
  0.0001)

(test-float "round(-3.5) = -4"
  -4.0
  (round -3.5)
  0.0001)

(test-float "round(-3.6) = -4"
  -4.0
  (round -3.6)
  0.0001)

(test-eq "round(5) = 5 (integer input)"
  5
  (round 5))

(test-eq "round(-5) = -5 (negative integer)"
  -5
  (round -5))

;; ============================================================
;; Test 6: round function - Edge cases
;; ============================================================

(print "")
(print "=== Test 6: round Edge Cases ===")

(test-float "round(0.4) = 0"
  0.0
  (round 0.4)
  0.0001)

(test-float "round(0.5) = 1"
  1.0
  (round 0.5)
  0.0001)

(test-float "round(-0.4) = 0"
  0.0
  (round -0.4)
  0.0001)

(test-float "round(-0.5) = -1"
  -1.0
  (round -0.5)
  0.0001)

(test-float "round(0.49) = 0"
  0.0
  (round 0.49)
  0.0001)

(test-float "round(0.51) = 1"
  1.0
  (round 0.51)
  0.0001)

(test-float "round(2.5) = 3"
  3.0
  (round 2.5)
  0.0001)

(test-float "round(-2.5) = -3"
  -3.0
  (round -2.5)
  0.0001)

;; ============================================================
;; Test 7: trunc function - Truncate towards zero
;; ============================================================

(print "")
(print "=== Test 7: trunc Function ===")

(test-float "trunc(3.9) = 3"
  3.0
  (trunc 3.9)
  0.0001)

(test-float "trunc(3.2) = 3"
  3.0
  (trunc 3.2)
  0.0001)

(test-float "trunc(-3.9) = -3"
  -3.0
  (trunc -3.9)
  0.0001)

(test-float "trunc(-3.2) = -3"
  -3.0
  (trunc -3.2)
  0.0001)

(test-eq "trunc(5) = 5 (integer input)"
  5
  (trunc 5))

(test-eq "trunc(-5) = -5 (negative integer)"
  -5
  (trunc -5))

(test-float "trunc(0.5) = 0"
  0.0
  (trunc 0.5)
  0.0001)

(test-float "trunc(-0.5) = 0"
  0.0
  (trunc -0.5)
  0.0001)

;; ============================================================
;; Test 8: trunc function - Edge cases
;; ============================================================

(print "")
(print "=== Test 8: trunc Edge Cases ===")

(test-eq "trunc(0) = 0 (zero)"
  0
  (trunc 0))

(test-float "trunc(-0.0) = 0 (negative zero)"
  0.0
  (trunc -0.0)
  0.0001)

(test-float "trunc(9999999.9) = 9999999"
  9999999.0
  (trunc 9999999.9)
  0.0001)

(test-float "trunc(-9999999.9) = -9999999"
  -9999999.0
  (trunc -9999999.9)
  0.0001)

(test-float "trunc(0.0001) = 0"
  0.0
  (trunc 0.0001)
  0.0001)

(test-float "trunc(-0.0001) = 0"
  0.0
  (trunc -0.0001)
  0.0001)

(test-float "trunc(-0.9999) = 0"
  0.0
  (trunc -0.9999)
  0.0001)

;; ============================================================
;; Test 9: Type coercion behavior
;; ============================================================

(print "")
(print "=== Test 9: Type Coercion ===")

;; floor with mixed types
(test-float "floor(3.0) returns float"
  3.0
  (floor 3.0)
  0.0001)

(test-float "floor(3) returns int"
  3
  (floor 3)
  0.0001)

;; ceil with mixed types
(test-float "ceil(4.0) returns float"
  4.0
  (ceil 4.0)
  0.0001)

(test-float "ceil(4) returns int"
  4
  (ceil 4)
  0.0001)

;; round with mixed types
(test-float "round(5.0) returns float"
  5.0
  (round 5.0)
  0.0001)

(test-float "round(5) returns int"
  5
  (round 5)
  0.0001)

;; trunc with mixed types
(test-float "trunc(6.0) returns float"
  6.0
  (trunc 6.0)
  0.0001)

(test-float "trunc(6) returns int"
  6
  (trunc 6)
  0.0001)

;; ============================================================
;; Test 10: Comparison between rounding methods
;; ============================================================

(print "")
(print "=== Test 10: Rounding Method Comparison ===")

;; Positive value 3.7
(test-float "floor(3.7) = 3"
  3.0
  (floor 3.7)
  0.0001)

(test-float "ceil(3.7) = 4"
  4.0
  (ceil 3.7)
  0.0001)

(test-float "round(3.7) = 4"
  4.0
  (round 3.7)
  0.0001)

(test-float "trunc(3.7) = 3"
  3.0
  (trunc 3.7)
  0.0001)

;; Negative value -3.7
(test-float "floor(-3.7) = -4"
  -4.0
  (floor -3.7)
  0.0001)

(test-float "ceil(-3.7) = -3"
  -3.0
  (ceil -3.7)
  0.0001)

(test-float "round(-3.7) = -4"
  -4.0
  (round -3.7)
  0.0001)

(test-float "trunc(-3.7) = -3"
  -3.0
  (trunc -3.7)
  0.0001)

;; Edge case: -0.5
(test-float "floor(-0.5) = -1"
  -1.0
  (floor -0.5)
  0.0001)

(test-float "ceil(-0.5) = 0"
  0.0
  (ceil -0.5)
  0.0001)

(test-float "round(-0.5) = -1"
  -1.0
  (round -0.5)
  0.0001)

(test-float "trunc(-0.5) = 0"
  0.0
  (trunc -0.5)
  0.0001)

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
