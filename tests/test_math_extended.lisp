;; test_math_extended.lisp - Tests for untested math functions
;;
;; Tests for math functions that are currently untested:
;; - Hyperbolic functions: sinh, cosh, tanh
;; - Mathematical constants: pi, e, inf, nan
;; - Comparison functions: min, max
;; - Bitwise operations: band, bor, bxor, bnot, lshift, rshift
;; - Special number checks: is_nan?, is_inf?, is_finite?
;; - Utility functions: abs, signum
;; - Random functions: seed-random, random, random-int, etc.
;;
;; Run with: ./omni tests/test_math_extended.lisp

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

(define test-true [name] [actual]
  (set! test-count (+ test-count 1))
  (if actual
      (do
        (set! pass-count (+ pass-count 1))
        (print "PASS:" name))
      (do
        (set! fail-count (+ fail-count 1))
        (print "FAIL:" name)
        (print "  Expected: true")
        (print "  Got: false"))))

(define test-false [name] [actual]
  (set! test-count (+ test-count 1))
  (if (not actual)
      (do
        (set! pass-count (+ pass-count 1))
        (print "PASS:" name))
      (do
        (set! fail-count (+ fail-count 1))
        (print "FAIL:" name)
        (print "  Expected: false")
        (print "  Got: true"))))

(define test-approx [name] [expected] [actual] [epsilon]
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
;; Test 1: Hyperbolic Functions (sinh, cosh, tanh)
;; ============================================================

(print "")
(print "=== Test 1: Hyperbolic Functions ===")

;; Test 1.1: sinh(0) = 0
(test-approx "sinh(0) = 0"
  0.0
  (sinh 0)
  0.0001)

;; Test 1.2: sinh(1) ≈ 1.1752
(test-approx "sinh(1) ≈ 1.1752"
  1.1752
  (sinh 1)
  0.0001)

;; Test 1.3: cosh(0) = 1
(test-approx "cosh(0) = 1"
  1.0
  (cosh 0)
  0.0001)

;; Test 1.4: cosh(1) ≈ 1.5431
(test-approx "cosh(1) ≈ 1.5431"
  1.5431
  (cosh 1)
  0.0001)

;; Test 1.5: tanh(0) = 0
(test-approx "tanh(0) = 0"
  0.0
  (tanh 0)
  0.0001)

;; Test 1.6: tanh(1) ≈ 0.7616
(test-approx "tanh(1) ≈ 0.7616"
  0.7616
  (tanh 1)
  0.0001)

;; Test 1.7: tanh(-1) ≈ -0.7616
(test-approx "tanh(-1) ≈ -0.7616"
  -0.7616
  (tanh -1)
  0.0001)

;; ============================================================
;; Test 2: Mathematical Constants
;; ============================================================

(print "")
(print "=== Test 2: Mathematical Constants ===")

;; Test 2.1: pi constant
(test-approx "pi ≈ 3.14159"
  3.14159
  pi
  0.00001)

;; Test 2.2: e constant
(test-approx "e ≈ 2.71828"
  2.71828
  e
  0.00001)

;; Test 2.3: inf constant (should be infinite)
(do
  (define inf-val inf)
  (test-true "inf is positive" (> inf-val 1000000000)))

;; Test 2.4: nan constant (should not equal itself)
(do
  (define nan-val nan)
  (test-true "nan is not equal to itself" (not (= nan-val nan-val))))

;; ============================================================
;; Test 3: Comparison Functions (min, max)
;; ============================================================

(print "")
(print "=== Test 3: Comparison Functions ===")

;; Test 3.1: min with positive numbers
(test-eq "min(5, 3) = 3"
  3
  (min 5 3))

;; Test 3.2: min with negative numbers
(test-eq "min(-5, -3) = -5"
  -5
  (min -5 -3))

;; Test 3.3: max with positive numbers
(test-eq "max(5, 3) = 5"
  5
  (max 5 3))

;; Test 3.4: max with negative numbers
(test-eq "max(-5, -3) = -3"
  -3
  (max -5 -3))

;; Test 3.5: min with equal numbers
(test-eq "min(5, 5) = 5"
  5
  (min 5 5))

;; ============================================================
;; Test 4: Bitwise Operations
;; ============================================================

(print "")
(print "=== Test 4: Bitwise Operations ===")

;; Test 4.1: band (bitwise AND)
(test-eq "band(5, 3) = 1"
  1
  (band 5 3))  ;; 5 = 101b, 3 = 011b, 101 & 011 = 001 = 1

;; Test 4.2: band with zeros
(test-eq "band(5, 0) = 0"
  0
  (band 5 0))

;; Test 4.3: bor (bitwise OR)
(test-eq "bor(5, 3) = 7"
  7
  (bor 5 3))  ;; 101b | 011b = 111b = 7

;; Test 4.4: bxor (bitwise XOR)
(test-eq "bxor(5, 3) = 6"
  6
  (bxor 5 3))  ;; 101b ^ 011b = 110b = 6

;; Test 4.5: bnot (bitwise NOT) - depends on word size
;; We'll test with a simpler property: bnot(0) should be -1
(test-eq "bnot(0) = -1"
  -1
  (bnot 0))

;; Test 4.6: lshift (left shift)
(test-eq "lshift(1, 3) = 8"
  8
  (lshift 1 3))  ;; 1 << 3 = 8

;; Test 4.7: lshift with zero
(test-eq "lshift(5, 0) = 5"
  5
  (lshift 5 0))

;; Test 4.8: rshift (right shift)
(test-eq "rshift(8, 3) = 1"
  1
  (rshift 8 3))  ;; 8 >> 3 = 1

;; Test 4.9: rshift with zero
(test-eq "rshift(5, 0) = 5"
  5
  (rshift 5 0))

;; ============================================================
;; Test 5: Special Number Checks
;; ============================================================

(print "")
(print "=== Test 5: Special Number Checks ===")

;; Test 5.1: is_nan? on actual NaN
(test-true "is_nan?(nan) is true"
  (is_nan? nan))

;; Test 5.2: is_nan? on regular number
(test-false "is_nan?(5) is false"
  (is_nan? 5))

;; Test 5.3: is_inf? on infinity
(test-true "is_inf?(inf) is true"
  (is_inf? inf))

;; Test 5.4: is_inf? on negative infinity
(test-true "is_inf?(-inf) is true"
  (is-inf? (- inf)))

;; Test 5.5: is_inf? on regular number
(test-false "is_inf?(5) is false"
  (is-inf? 5))

;; Test 5.6: is_finite? on regular number
(test-true "is_finite?(5) is true"
  (is-finite? 5))

;; Test 5.7: is_finite? on infinity (should be false)
(test-false "is_finite?(inf) is false"
  (is-finite? inf))

;; Test 5.8: is_finite? on NaN (should be false)
(test-false "is_finite?(nan) is false"
  (is-finite? nan))

;; ============================================================
;; Test 6: Utility Functions (abs, signum)
;; ============================================================

(print "")
(print "=== Test 6: Utility Functions ===")

;; Test 6.1: abs of positive number
(test-eq "abs(5) = 5"
  5
  (abs 5))

;; Test 6.2: abs of negative number
(test-eq "abs(-5) = 5"
  5
  (abs -5))

;; Test 6.3: abs of zero
(test-eq "abs(0) = 0"
  0
  (abs 0))

;; Test 6.4: signum of positive number
(test-eq "signum(5) = 1"
  1
  (signum 5))

;; Test 6.5: signum of negative number
(test-eq "signum(-5) = -1"
  -1
  (signum -5))

;; Test 6.6: signum of zero
(test-eq "signum(0) = 0"
  0
  (signum 0))

;; ============================================================
;; Test 7: Random Functions
;; ============================================================

(print "")
(print "=== Test 7: Random Functions ===")

;; Test 7.1: seed-random with specific seed
;; We can't test exact values, but we can verify it returns something
(do
  (seed-random 42)
  (define r1 (random))
  (test-true "random returns number" (number? r1)))

;; Test 7.2: random-int returns integer in range [0, n)
(do
  (seed-random 123)
  (define r-int (random-int 100))
  (test-true "random-int returns integer" (integer? r-int))
  (test-true "random-int within range [0, 100)" (and (>= r-int 0) (< r-int 100))))

;; Test 7.3: random-range returns number in range [min, max]
(do
  (seed-random 456)
  (define r-range (random-range 5.0 10.0))
  (test-true "random-range returns number" (number? r-range))
  (test-true "random-range within [5.0, 10.0]" (and (>= r-range 5.0) (<= r-range 10.0))))

;; Test 7.4: random-float-range returns float in range
(do
  (seed-random 789)
  (define r-float (random-float-range 0.0 1.0))
  (test-true "random-float-range returns number" (number? r-float))
  (test-true "random-float-range within [0.0, 1.0]" (and (>= r-float 0.0) (<= r-float 1.0))))

;; Test 7.5: random-choice from array
(do
  (seed-random 999)
  (define choices (array 1 2 3 4 5))
  (define chosen (random-choice choices))
  (test-true "random-choice returns element from array" (and (integer? chosen) (>= chosen 1) (<= chosen 5))))

;; ============================================================
;; Test 8: Hyperbolic Identity (cosh² - sinh² = 1)
;; ============================================================

(print "")
(print "=== Test 8: Hyperbolic Identity ===")

;; Test 8.1: cosh²(x) - sinh²(x) = 1
(do
  (define x 1.0)
  (define result (- (* (cosh x) (cosh x)) (* (sinh x) (sinh x))))
  (test-approx "cosh²(1) - sinh²(1) = 1"
    1.0
    result
    0.0001))

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
