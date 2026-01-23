;; test_interpose.lisp - Tests for interpose collection primitive
;;
;; Tests interpose function which inserts a separator between each pair
;; of elements in a collection. The separator is NOT added at beginning
;; or end of result.
;;
;; Examples:
;;   (interpose 0 [1 2 3]) => [1 0 2 0 3]
;;   (interpose "," ["a" "b" "c"]) => ["a" "," "b" "," "c"]
;;
;; Run with: ./omni tests/test_interpose.lisp

;; ============================================================
;; Test Framework
;; ============================================================

(define test-count 0)
(define pass-count 0)
(define fail-count 0)

(define test-array [name] [expected] [actual]
  (set! test-count (+ test-count 1))
  (if (equal? expected actual)
      (do
        (set! pass-count (+ pass-count 1))
        (print "PASS:" name))
      (do
        (set! fail-count (+ fail-count 1))
        (print "FAIL:" name)
        (print "  Expected:" expected)
        (print "  Got:" actual))))

;; ============================================================
;; Test 1: Basic array interpose
;; ============================================================

(print "")
(print "=== Test 1: Basic Array Interpose ===")

(test-array "interpose with single separator"
  [1 0 2 0 3]
  (interpose 0 [1 2 3]))

(test-array "interpose with string separator"
  ["a" "," "b" "," "c"]
  (interpose "," ["a" "b" "c"]))

;; ============================================================
;; Test 2: Empty collection
;; ============================================================

(print "")
(print "=== Test 2: Empty Collection ===")

(test-array "interpose on empty array"
  []
  (interpose 0 []))

;; ============================================================
;; Test 3: Single element collection
;; ============================================================

(print "")
(print "=== Test 3: Single Element Collection ===")

(test-array "interpose on single element array"
  [1]
  (interpose 0 [1]))

(test-array "interpose on single element string"
  ["hello"]
  (interpose "," ["hello"]))

;; ============================================================
;; Test 4: List interpose
;; ============================================================

(print "")
(print "=== Test 4: List Interpose ===")

(test-array "interpose on list returns array"
  [1 0 2 0 3]
  (interpose 0 (list 1 2 3)))

(test-array "interpose on list of strings"
  ["a" "-" "b" "-" "c"]
  (interpose "-" (list "a" "b" "c")))

;; ============================================================
;; Test 5: Complex separators
;; ============================================================

(print "")
(print "=== Test 5: Complex Separators ===")

(test-array "interpose with array separator"
  [1 [0] 2 [0] 3]
  (interpose [0] [1 2 3]))

(test-array "interpose with dict separator"
  [1 #{} 2 #{} 3]
  (interpose #{} [1 2 3]))

;; ============================================================
;; Test 6: Different data types
;; ============================================================

(print "")
(print "=== Test 6: Different Data Types ===")

(test-array "interpose with float numbers"
  [1.0 0.5 2.0 0.5 3.0]
  (interpose 0.5 [1.0 2.0 3.0]))

(test-array "interpose with boolean values"
  [true false false false false]
  (interpose false [true false false false]))

;; ============================================================
;; Test 7: No separator
;; ============================================================

(print "")
(print "=== Test 7: No Separator ===")

(test-array "interpose with nothing separator"
  [1 nothing 2 nothing 3]
  (interpose nothing [1 2 3]))

;; ============================================================
;; Test 8: Two element collections
;; ============================================================

(print "")
(print "=== Test 8: Two Element Collections ===")

(test-array "interpose on two elements"
  [1 "," 2]
  (interpose "," [1 2]))

(test-array "interpose on two strings"
  ["hello" " " "world"]
  (interpose " " ["hello" "world"]))

;; ============================================================
;; Test 9: Long collections
;; ============================================================

(print "")
(print "=== Test 9: Long Collections ===")

(test-array "interpose on 5 elements"
  [1 0 2 0 3 0 4 0 5]
  (interpose 0 [1 2 3 4 5]))

(test-array "interpose on 10 elements"
  [1 "," 2 "," 3 "," 4 "," 5 "," 6 "," 7 "," 8 "," 9 "," 10]
  (interpose "," [1 2 3 4 5 6 7 8 9 10]))

;; ============================================================
;; Test 10: Verify separator NOT at boundaries
;; ============================================================

(print "")
(print "=== Test 10: Boundary Tests ===")

;; Test that separator is NOT at the beginning
(define result1 (interpose 0 [1 2 3]))
(test-array "separator NOT at beginning"
  [1 0 2 0 3]
  result1)

;; Test that separator is NOT at the end
(test-array "separator NOT at end"
  [1 0 2 0 3]
  result1)

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
