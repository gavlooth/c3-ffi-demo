;; test_math_lisp.lisp - High-level Lisp tests for math functions
;;
;; Run with: ./omni tests/test_math_lisp.lisp

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
;; Inverse Trigonometric Tests
;; ============================================================

(print "")
(print "=== Inverse Trigonometric Tests ===")

;; asin tests
(test-float "asin(0)" 0.0 (asin 0) 0.0001)
(test-float "asin(0.5)" (/ 3.14159 6.0) (asin 0.5) 0.0001)
(test-float "asin(-0.5)" (/ (- 3.14159) 6.0) (asin -0.5) 0.0001)
(test-float "asin(1)" (/ 3.14159 2.0) (asin 1) 0.0001)

;; acos tests
(test-float "acos(0)" (/ 3.14159 2.0) (acos 0) 0.0001)
(test-float "acos(0.5)" (/ 3.14159 3.0) (acos 0.5) 0.0001)
(test-float "acos(-0.5)" (* (/ 3.14159 3.0) 2.0) (acos -0.5) 0.0001)
(test-float "acos(1)" 0.0 (acos 1) 0.0001)

;; atan tests
(test-float "atan(0)" 0.0 (atan 0) 0.0001)
(test-float "atan(1)" (/ 3.14159 4.0) (atan 1) 0.0001)
(test-float "atan(-1)" (/ (- 3.14159) 4.0) (atan -1) 0.0001)

;; ============================================================
;; atan2 Tests (Quadrant Handling)
;; ============================================================

(print "")
(print "=== atan2 Tests ===")

;; Quadrant I
(test-float "atan2(1,1)" (/ 3.14159 4.0) (atan2 1 1) 0.0001)

;; Quadrant II
(test-float "atan2(1,-1)" (* (/ 3.14159 4.0) 3.0) (atan2 1 -1) 0.0001)

;; Quadrant III
(test-float "atan2(-1,-1)" (* (/ (- 3.14159) 4.0) 3.0) (atan2 -1 -1) 0.0001)

;; Quadrant IV
(test-float "atan2(-1,1)" (/ (- 3.14159) 4.0) (atan2 -1 1) 0.0001)

;; Axis alignment
(test-float "atan2(0,5)" 0.0 (atan2 0 5) 0.0001)
(test-float "atan2(0,-5)" 3.14159 (atan2 0 -5) 0.0001)
(test-float "atan2(5,0)" (/ 3.14159 2.0) (atan2 5 0) 0.0001)
(test-float "atan2(-5,0)" (/ (- 3.14159) 2.0) (atan2 -5 0) 0.0001)

;; ============================================================
;; Exponential/Logarithmic Tests
;; ============================================================

(print "")
(print "=== Exponential/Logarithmic Tests ===")

(test-float "exp(0)" 1.0 (exp 0) 0.0001)
(test-float "exp(1)" 2.71828 (exp 1) 0.0001)

(test-float "log(1)" 0.0 (log 1) 0.0001)
(test-float "log(2.71828)" 1.0 (log 2.71828) 0.0001)

(test-float "log10(1)" 0.0 (log10 1) 0.0001)
(test-float "log10(10)" 1.0 (log10 10) 0.0001)
(test-float "log10(100)" 2.0 (log10 100) 0.0001)

(test-float "log2(1)" 0.0 (log2 1) 0.0001)
(test-float "log2(2)" 1.0 (log2 2) 0.0001)
(test-float "log2(8)" 3.0 (log2 8) 0.0001)

(test-float "sqrt(0)" 0.0 (sqrt 0) 0.0001)
(test-float "sqrt(4)" 2.0 (sqrt 4) 0.0001)
(test-float "sqrt(9)" 3.0 (sqrt 9) 0.0001)
(test-float "sqrt(2)" 1.41421 (sqrt 2) 0.0001)

;; ============================================================
;; Rounding Tests
;; ============================================================

(print "")
(print "=== Rounding Tests ===")

(test-float "floor(3.7)" 3.0 (floor 3.7) 0.0001)
(test-float "floor(-3.7)" -4.0 (floor -3.7) 0.0001)

(test-float "ceil(3.2)" 4.0 (ceil 3.2) 0.0001)
(test-float "ceil(-3.2)" -3.0 (ceil -3.2) 0.0001)

(test-float "round(3.5)" 4.0 (round 3.5) 0.0001)
(test-float "round(3.4)" 3.0 (round 3.4) 0.0001)
(test-float "round(-3.5)" -4.0 (round -3.5) 0.0001)

(test-float "trunc(3.9)" 3.0 (trunc 3.9) 0.0001)
(test-float "trunc(-3.9)" -3.0 (trunc -3.9) 0.0001)

;; ============================================================
;; Math Constants Tests
;; ============================================================

(print "")
(print "=== Math Constants Tests ===")

(test-float "pi constant" 3.14159 pi 0.0001)
(test-float "e constant" 2.71828 e 0.0001)

;; ============================================================
;; Comparison Tests
;; ============================================================

(print "")
(print "=== Comparison Tests ===")

(test-float "min(5,3)" 3.0 (min 5 3) 0.0001)
(test-float "min(-5,-3)" -5.0 (min -5 -3) 0.0001)

(test-float "max(5,3)" 5.0 (max 5 3) 0.0001)
(test-float "max(-5,-3)" -3.0 (max -5 -3) 0.0001)

(test-float "clamp(5,0,10)" 5.0 (clamp 5 0 10) 0.0001)
(test-float "clamp(-5,0,10)" 0.0 (clamp -5 0 10) 0.0001)
(test-float "clamp(15,0,10)" 10.0 (clamp 15 0 10) 0.0001)

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
