;; test_pow.lisp - Tests for pow (power) function
;;
;; Tests the pow function which computes base^exponent.
;; This is a fundamental mathematical operation used extensively in
;; scientific computing, graphics, algorithms, and many applications.
;;
;; Run with: ./omni tests/test_pow.lisp

;; ============================================================
;; Test Framework
;; ============================================================

(define test-count 0)
(define pass-count 0)
(define fail-count 0)

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
;; Test 1: Integer powers (positive bases)
;; ============================================================

(print "")
(print "=== Test 1: Integer Powers (Positive Bases) ===")

(test-float "2^3 = 8"
  8.0
  (pow 2 3)
  0.0001)

(test-float "5^2 = 25"
  25.0
  (pow 5 2)
  0.0001)

(test-float "10^3 = 1000"
  1000.0
  (pow 10 3)
  0.0001)

(test-float "3^4 = 81"
  81.0
  (pow 3 4)
  0.0001)

(test-float "7^1 = 7"
  7.0
  (pow 7 1)
  0.0001)

(test-float "100^2 = 10000"
  10000.0
  (pow 100 2)
  0.0001)

;; ============================================================
;; Test 2: Zero and one base cases
;; ============================================================

(print "")
(print "=== Test 2: Zero and One Base Cases ===")

(test-float "0^5 = 0"
  0.0
  (pow 0 5)
  0.0001)

(test-float "0^1 = 0"
  0.0
  (pow 0 1)
  0.0001)

(test-float "1^100 = 1 (identity)"
  1.0
  (pow 1 100)
  0.0001)

(test-float "1^0 = 1 (identity)"
  1.0
  (pow 1 0)
  0.0001)

(test-float "5^0 = 1 (any^0 = 1)"
  1.0
  (pow 5 0)
  0.0001)

(test-float "100^0 = 1"
  1.0
  (pow 100 0)
  0.0001)

;; ============================================================
;; Test 3: Negative bases
;; ============================================================

(print "")
(print "=== Test 3: Negative Bases ===")

(test-float "(-2)^2 = 4"
  4.0
  (pow -2 2)
  0.0001)

(test-float "(-2)^3 = -8"
  -8.0
  (pow -2 3)
  0.0001)

(test-float "(-5)^2 = 25"
  25.0
  (pow -5 2)
  0.0001)

(test-float "(-3)^3 = -27"
  -27.0
  (pow -3 3)
  0.0001)

(test-float "(-1)^10 = 1"
  1.0
  (pow -1 10)
  0.0001)

(test-float "(-1)^11 = -1"
  -1.0
  (pow -1 11)
  0.0001)

;; ============================================================
;; Test 4: Fractional exponents (roots)
;; ============================================================

(print "")
(print "=== Test 4: Fractional Exponents (Roots) ===")

(test-float "4^0.5 = 2 (square root)"
  2.0
  (pow 4 0.5)
  0.0001)

(test-float "9^0.5 = 3 (square root)"
  3.0
  (pow 9 0.5)
  0.0001)

(test-float "8^(1/3) ≈ 2 (cube root)"
  2.0
  (pow 8 (/ 1 3))
  0.001)

(test-float "16^0.25 = 2 (fourth root)"
  2.0
  (pow 16 0.25)
  0.0001)

(test-float "27^(1/3) = 3 (cube root)"
  3.0
  (pow 27 (/ 1 3))
  0.001)

(test-float "2^0.5 = 1.41421... (sqrt of 2)"
  1.41421
  (pow 2 0.5)
  0.0001)

;; ============================================================
;; Test 5: Fractional bases
;; ============================================================

(print "")
(print "=== Test 5: Fractional Bases ===")

(test-float "0.5^2 = 0.25"
  0.25
  (pow 0.5 2)
  0.0001)

(test-float "2.5^2 = 6.25"
  6.25
  (pow 2.5 2)
  0.0001)

(test-float "0.1^3 = 0.001"
  0.001
  (pow 0.1 3)
  0.0001)

(test-float "1.5^4 = 5.0625"
  5.0625
  (pow 1.5 4)
  0.0001)

(test-float "3.14^2 ≈ 9.8596 (pi squared)"
  9.8596
  (pow 3.14 2)
  0.0001)

;; ============================================================
;; Test 6: Negative exponents (reciprocals)
;; ============================================================

(print "")
(print "=== Test 6: Negative Exponents (Reciprocals) ===")

(test-float "2^(-1) = 0.5"
  0.5
  (pow 2 -1)
  0.0001)

(test-float "2^(-2) = 0.25"
  0.25
  (pow 2 -2)
  0.0001)

(test-float "10^(-3) = 0.001"
  0.001
  (pow 10 -3)
  0.0001)

(test-float "5^(-1) = 0.2"
  0.2
  (pow 5 -1)
  0.0001)

(test-float "4^(-0.5) = 0.5 (inverse sqrt)"
  0.5
  (pow 4 -0.5)
  0.0001)

;; ============================================================
;; Test 7: Large exponents
;; ============================================================

(print "")
(print "=== Test 7: Large Exponents ===")

(test-float "2^10 = 1024"
  1024.0
  (pow 2 10)
  0.001)

(test-float "2^20 = 1048576"
  1048576.0
  (pow 2 20)
  0.1)

(test-float "10^6 = 1000000"
  1000000.0
  (pow 10 6)
  0.1)

;; ============================================================
;; Test 8: Combined operations with pow
;; ============================================================

(print "")
(print "=== Test 8: Combined Operations ===")

;; Test using pow in arithmetic
(test-float "pow(2,3) + pow(2,4) = 8 + 16 = 24"
  24.0
  (+ (pow 2 3) (pow 2 4))
  0.0001)

;; Test using pow in multiplication
(test-float "pow(3,2) * 2 = 9 * 2 = 18"
  18.0
  (* (pow 3 2) 2)
  0.0001)

;; Test nested pow
(test-float "pow(2, pow(2,2)) = 2^4 = 16"
  16.0
  (pow 2 (pow 2 2))
  0.0001)

;; Test pow with variable binding
(define x 5)
(define y 2)
(test-float "pow with variable binding 5^2 = 25"
  25.0
  (pow x y)
  0.0001)

;; ============================================================
;; Test 9: Floating point precision tests
;; ============================================================

(print "")
(print "=== Test 9: Floating Point Precision ===")

;; Test that very close values are handled
(test-float "e^1 ≈ 2.71828"
  2.71828
  (pow 2.71828 1)
  0.0001)

(test-float "pi^2 ≈ 9.8696"
  9.8696
  (pow 3.14159 2)
  0.0001)

;; Test small fractional differences
(test-float "0.5^10 = 0.0009765625"
  0.0009765625
  (pow 0.5 10)
  0.000001)

;; ============================================================
;; Test 10: Mathematical properties
;; ============================================================

(print "")
(print "=== Test 10: Mathematical Properties ===")

;; Test commutativity of exponentiation: a^(b+c) = a^b * a^c
(define a 2)
(define b 3)
(define c 4)
(test-float "a^(b+c) = a^b * a^c"
  (pow a (+ b c))
  (* (pow a b) (pow a c))
  0.001)

;; Test power of power: (a^b)^c = a^(b*c)
(test-float "(a^b)^c = a^(b*c)"
  (pow (pow 2 3) 2)
  (pow 2 (* 3 2))
  0.001)

;; Test with negative base and even exponent (should be positive)
(test-float "(-10)^2 = 100 (negative base, even exp)"
  100.0
  (pow -10 2)
  0.0001)

;; ============================================================
;; Test 11: Edge cases and boundaries
;; ============================================================

(print "")
(print "=== Test 11: Edge Cases and Boundaries ===")

;; Very small base with large exponent
(test-float "0.001^2 = 0.000001"
  0.000001
  (pow 0.001 2)
  0.0000001)

;; Large base with fractional exponent
(test-float "1000^0.333... ≈ 10 (cube root)"
  10.0
  (pow 1000 (/ 1 3))
  0.1)

;; ============================================================
;; Test 12: Idempotency and special forms
;; ============================================================

(print "")
(print "=== Test 12: Idempotency ===")

;; pow(a, 1) should equal a
(test-float "pow(x, 1) = x"
  42.5
  (pow 42.5 1)
  0.0001)

;; pow(a, 0) should equal 1 for any a
(test-float "pow(x, 0) = 1 for x=99"
  1.0
  (pow 99 0)
  0.0001)

(test-float "pow(x, 0) = 1 for x=-5"
  1.0
  (pow -5 0)
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
