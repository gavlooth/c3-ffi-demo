;; test_bitwise_shift.lisp - Tests for bitwise shift operations
;;
;; Tests:
;;   - lshift: Left shift operation (multiply by powers of 2)
;;   - rshift: Right shift operation (divide by powers of 2)
;;
;; Run with: ./omni tests/test_bitwise_shift.lisp

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

;; ============================================================
;; Test 1: Basic left shift
;; ============================================================

(print "")
(print "=== Test 1: Basic Left Shift (lshift) ===")

(test-num "lshift 1 by 1 bit"
  2
  (lshift 1 1))

(test-num "lshift 1 by 2 bits"
  4
  (lshift 1 2))

(test-num "lshift 1 by 3 bits"
  8
  (lshift 1 3))

(test-num "lshift 1 by 10 bits"
  1024
  (lshift 1 10))

;; ============================================================
;; Test 2: Left shift with larger numbers
;; ============================================================

(print "")
(print "=== Test 2: Left Shift with Larger Numbers ===")

(test-num "lshift 5 by 1 bit (5 * 2 = 10)"
  10
  (lshift 5 1))

(test-num "lshift 5 by 2 bits (5 * 4 = 20)"
  20
  (lshift 5 2))

(test-num "lshift 10 by 3 bits (10 * 8 = 80)"
  80
  (lshift 10 3))

(test-num "lshift 100 by 2 bits (100 * 4 = 400)"
  400
  (lshift 100 2))

;; ============================================================
;; Test 3: Left shift edge cases
;; ============================================================

(print "")
(print "=== Test 3: Left Shift Edge Cases ===")

(test-num "lshift 0 by 5 bits"
  0
  (lshift 0 5))

(test-num "lshift 1 by 0 bits"
  1
  (lshift 1 0))

(test-num "lshift 42 by 0 bits"
  42
  (lshift 42 0))

;; ============================================================
;; Test 4: Basic right shift
;; ============================================================

(print "")
(print "=== Test 4: Basic Right Shift (rshift) ===")

(test-num "rshift 8 by 1 bit"
  4
  (rshift 8 1))

(test-num "rshift 8 by 2 bits"
  2
  (rshift 8 2))

(test-num "rshift 8 by 3 bits"
  1
  (rshift 8 3))

(test-num "rshift 1024 by 10 bits"
  1
  (rshift 1024 10))

;; ============================================================
;; Test 5: Right shift with larger numbers
;; ============================================================

(print "")
(print "=== Test 5: Right Shift with Larger Numbers ===")

(test-num "rshift 100 by 1 bit (100 / 2 = 50)"
  50
  (rshift 100 1))

(test-num "rshift 100 by 2 bits (100 / 4 = 25)"
  25
  (rshift 100 2))

(test-num "rshift 80 by 3 bits (80 / 8 = 10)"
  10
  (rshift 80 3))

(test-num "rshift 400 by 2 bits (400 / 4 = 100)"
  100
  (rshift 400 2))

;; ============================================================
;; Test 6: Right shift edge cases
;; ============================================================

(print "")
(print "=== Test 6: Right Shift Edge Cases ===")

(test-num "rshift 0 by 5 bits"
  0
  (rshift 0 5))

(test-num "rshift 1 by 0 bits"
  1
  (rshift 1 0))

(test-num "rshift 42 by 0 bits"
  42
  (rshift 42 0))

(test-num "rshift 1 by 10 bits (1 / 1024 = 0)"
  0
  (rshift 1 10))

;; ============================================================
;; Test 7: Shift with powers of 2
;; ============================================================

(print "")
(print "=== Test 7: Shift with Powers of 2 ===")

(test-num "lshift 2 by 4 bits (2 * 16 = 32)"
  32
  (lshift 2 4))

(test-num "lshift 4 by 3 bits (4 * 8 = 32)"
  32
  (lshift 4 3))

(test-num "lshift 8 by 2 bits (8 * 4 = 32)"
  32
  (lshift 8 2))

(test-num "lshift 16 by 1 bit (16 * 2 = 32)"
  32
  (lshift 16 1))

;; ============================================================
;; Test 8: Combined shift operations
;; ============================================================

(print "")
(print "=== Test 8: Combined Shift Operations ===")

(define val 5)
(test-num "lshift then rshift (5 << 2 >> 2 = 5)"
  5
  (rshift (lshift val 2) 2))

(test-num "rshift then lshift (10 >> 1 << 1 = 10)"
  10
  (lshift (rshift 10 1) 1))

;; ============================================================
;; Test 9: Large shift amounts
;; ============================================================

(print "")
(print "=== Test 9: Large Shift Amounts ===")

(test-num "lshift 1 by 20 bits (2^20 = 1048576)"
  1048576
  (lshift 1 20))

(test-num "rshift 1048576 by 20 bits"
  1
  (rshift 1048576 20))

;; ============================================================
;; Test 10: Shift with negative numbers
;; ============================================================

(print "")
(print "=== Test 10: Shift with Negative Numbers ===")

;; Note: In two's complement, left shift of negative numbers is implementation-defined.
;; Right shift of negative numbers is usually arithmetic (sign-extending).
(test-num "rshift -8 by 1 bit"
  -4
  (rshift -8 1))

(test-num "rshift -16 by 2 bits"
  -4
  (rshift -16 2))

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
