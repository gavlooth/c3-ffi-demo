;; test_group_by.lisp - Tests for group-by collection function
;;
;; Run with: ./omni tests/test_group_by.lisp

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

(define test-array-length [name] [expected] [arr]
  (set! test-count (+ test-count 1))
  (if (= expected (array-length arr))
      (do
        (set! pass-count (+ pass-count 1))
        (print "PASS:" name))
      (do
        (set! fail-count (+ fail-count 1))
        (print "FAIL:" name)
        (print "  Expected length:" expected)
        (print "  Got length:" (array-length arr)))))

;; ============================================================
;; Test 1: Basic grouping by identity
;; ============================================================

(print "")
(print "=== Test 1: Basic Grouping by Identity ===")

(define test1
  (group-by (fn [x] x) [1 2 2 3 3 3]))

;; Should have 3 groups: [1], [2,2], [3,3,3]
(test-array-length "group-by identity creates correct number of groups" 3 test1)

;; ============================================================
;; Test 2: Grouping numbers by parity (even/odd)
;; ============================================================

(print "")
(print "=== Test 2: Grouping by Parity ===")

(define test2
  (group-by (fn [x] (mod x 2)) [1 2 3 4 5 6 7 8]))

;; Should have 2 groups: odd (1,3,5,7) and even (2,4,6,8)
(test-array-length "group-by parity creates 2 groups" 2 test2)

;; ============================================================
;; Test 3: Grouping strings by length
;; ============================================================

(print "")
(print "=== Test 3: Grouping Strings by Length ===")

(define test3
  (group-by (fn [s] (string-length s)) ["a" "bb" "c" "dd" "ee"]))

;; Should have groups: length 1 (a,c), length 2 (bb,dd,ee)
(test-array-length "group-by string length creates correct groups" 2 test3)

;; ============================================================
;; Test 4: Grouping with key function
;; ============================================================

(print "")
(print "=== Test 4: Grouping with Key Function ===")

(define test4
  (group-by (fn [x] (if (> x 5) "large" "small")) [1 3 6 8 2 9]))

;; Should have 2 groups: "large" (6,8,9) and "small" (1,3,2)
(test-array-length "group-by with string keys" 2 test4)

;; ============================================================
;; Test 5: Grouping lists
;; ============================================================

(print "")
(print "=== Test 5: Grouping Lists ===")

(define test5
  (group-by (fn [x] (mod x 3)) (list 1 2 3 4 5 6)))

;; Should have 3 groups by modulo 3
(test-array-length "group-by on list" 3 test5)

;; ============================================================
;; Test 6: Grouping empty collection
;; ============================================================

(print "")
(print "=== Test 6: Empty Collection ===")

(define test6
  (group-by (fn [x] x) []))

;; Should return empty dict
(test-array-length "group-by on empty array returns empty" 0 test6)

;; ============================================================
;; Test 7: Grouping single element
;; ============================================================

(print "")
(print "=== Test 7: Single Element ===")

(define test7
  (group-by (fn [x] x) [42]))

;; Should have 1 group with 1 element
(test-array-length "group-by single element" 1 test7)

;; ============================================================
;; Test 8: Grouping with duplicate keys
;; ============================================================

(print "")
(print "=== Test 8: All Elements Map to Same Key ===")

(define test8
  (group-by (fn [x] "same") [1 2 3 4 5]))

;; Should have 1 group with all elements
(test-array-length "group-by all same key" 1 test8)

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
