;; test_numeric_predicates.lisp - Tests for numeric predicate and utility functions
;;
;; Tests:
;;   - abs: Absolute value function
;;   - signum: Sign function (returns -1, 0, or 1)
;;   - is-nan: Check if value is NaN (Not a Number)
;;   - is-inf: Check if value is infinite
;;   - is-finite: Check if value is finite
;;
;; Run with: ./omni tests/test_numeric_predicates.lisp

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

;; ============================================================
;; Test 1: abs function with integers
;; ============================================================

(print "")
(print "=== Test 1: abs Function (Integers) ===")

(test-num "abs of positive integer"
  42
  (abs 42))

(test-num "abs of negative integer"
  42
  (abs -42))

(test-num "abs of zero"
  0
  (abs 0))

;; ============================================================
;; Test 2: abs function with floats
;; ============================================================

(print "")
(print "=== Test 2: abs Function (Floats) ===")

(test-num "abs of positive float"
  3.14
  (abs 3.14))

(test-num "abs of negative float"
  3.14
  (abs -3.14))

(test-num "abs of zero float"
  0.0
  (abs 0.0))

;; ============================================================
;; Test 3: signum function with integers
;; ============================================================

(print "")
(print "=== Test 3: signum Function (Integers) ===")

(test-num "signum of positive integer"
  1
  (signum 42))

(test-num "signum of negative integer"
  -1
  (signum -42))

(test-num "signum of zero"
  0
  (signum 0))

;; ============================================================
;; Test 4: signum function with floats
;; ============================================================

(print "")
(print "=== Test 4: signum Function (Floats) ===")

(test-num "signum of positive float"
  1
  (signum 3.14))

(test-num "signum of negative float"
  -1
  (signum -3.14))

(test-num "signum of zero float"
  0
  (signum 0.0))

;; ============================================================
;; Test 5: is-nan function
;; ============================================================

(print "")
(print "=== Test 5: is-nan Function ===")

(test-bool "is-nan of integer is false"
  false
  (is-nan 42))

(test-bool "is-nan of zero is false"
  false
  (is-nan 0))

(test-bool "is-nan of positive float is false"
  false
  (is-nan 3.14))

(test-bool "is-nan of negative float is false"
  false
  (is-nan -3.14))

;; ============================================================
;; Test 6: is-inf function
;; ============================================================

(print "")
(print "=== Test 6: is-inf Function ===")

(test-bool "is-inf of integer is false"
  false
  (is-inf 42))

(test-bool "is-inf of zero is false"
  false
  (is-inf 0))

(test-bool "is-inf of positive float is false"
  false
  (is-inf 3.14))

(test-bool "is-inf of negative float is false"
  false
  (is-inf -3.14))

;; ============================================================
;; Test 7: is-finite function with integers
;; ============================================================

(print "")
(print "=== Test 7: is-finite Function (Integers) ===")

(test-bool "is-finite of positive integer is true"
  true
  (is-finite 42))

(test-bool "is-finite of negative integer is true"
  true
  (is-finite -42))

(test-bool "is-finite of zero is true"
  true
  (is-finite 0))

;; ============================================================
;; Test 8: is-finite function with floats
;; ============================================================

(print "")
(print "=== Test 8: is-finite Function (Floats) ===")

(test-bool "is-finite of positive float is true"
  true
  (is-finite 3.14))

(test-bool "is-finite of negative float is true"
  true
  (is-finite -3.14))

(test-bool "is-finite of zero float is true"
  true
  (is-finite 0.0))

;; ============================================================
;; Test 9: Edge cases for signum
;; ============================================================

(print "")
(print "=== Test 9: Edge Cases for signum ===")

(test-num "signum of very small positive number"
  1
  (signum 0.001))

(test-num "signum of very small negative number"
  -1
  (signum -0.001))

(test-num "signum of large positive number"
  1
  (signum 1000000))

(test-num "signum of large negative number"
  -1
  (signum -1000000))

;; ============================================================
;; Test 10: Edge cases for abs
;; ============================================================

(print "")
(print "=== Test 10: Edge Cases for abs ===")

(test-num "abs of -1"
  1
  (abs -1))

(test-num "abs of 1"
  1
  (abs 1))

(test-num "abs of very large positive number"
  1000000
  (abs 1000000))

(test-num "abs of very large negative number"
  1000000
  (abs -1000000))

;; ============================================================
;; Test 11: Boundary values
;; ============================================================

(print "")
(print "=== Test 11: Boundary Values ===")

;; Note: These tests verify that functions handle common boundary cases
(test-num "abs of -0.0"
  0.0
  (abs -0.0))

(test-num "signum of -0.0"
  0
  (signum -0.0))

(test-num "abs of 0.0"
  0.0
  (abs 0.0))

(test-num "signum of 0.0"
  0
  (signum 0.0))

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
