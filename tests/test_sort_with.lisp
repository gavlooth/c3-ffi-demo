;; test_sort_with.lisp - Tests for sort-with function
;;
;; Tests sort-with function which sorts collections using a custom comparator.
;; This is important for custom ordering scenarios (e.g., descending sort,
;; complex object sorting, domain-specific ordering).
;;
;; Run with: ./omni tests/test_sort_with.lisp

;; ============================================================
;; Test Framework
;; ============================================================

(define test-count 0)
(define pass-count 0)
(define fail-count 0)

(define test-equal [name] [expected] [actual]
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

(define test-list [name] [expected] [actual]
  (set! test-count (+ test-count 1))
  (if (list-equal expected actual)
      (do
        (set! pass-count (+ pass-count 1))
        (print "PASS:" name))
      (do
        (set! fail-count (+ fail-count 1))
        (print "FAIL:" name)
        (print "  Expected:" expected)
        (print "  Got:" actual))))

;; ============================================================
;; Test 1: Sort in descending order
;; ============================================================

(print "")
(print "=== Test 1: Descending Sort ===")

(test-list "sort descending numbers"
  [5 4 3 2 1]
; REVIEWED:SYNTAX
  ; REVIEWED:SYNTAX
  (sort-with (lambda (a b)
                (if (> a b) -1 (if (< a b) 1 0)))
             [1 2 3 4 5]))

;; ============================================================
;; Test 2: Sort by string length
;; ============================================================

(print "")
(print "=== Test 2: Sort by String Length ===")

; REVIEWED:SYNTAX
(test-list "sort by string length descending"
  ["elephant" "cat" "dog" "ant"]
  (sort-with (lambda (a b)
                (let ([len-a (string-length a)]
                      [len-b (string-length b)])
                  (if (> len-a len-b) -1 (if (< len-a len-b) 1 0))))
             ["cat" "elephant" "dog" "ant"]))

;; ============================================================
;; Test 3: Empty collection
;; ============================================================

(print "")
(print "=== Test 3: Empty Collection ===")

(test-list "sort empty list"
  []
  (sort-with (lambda (a b) 0) []))

(test-list "sort empty array"
  []
  (sort-with (lambda (a b) 0) (array)))

;; ============================================================
;; Test 4: Single element
;; ============================================================

(print "")
(print "=== Test 4: Single Element ===")

(test-list "sort single element"
  [42]
  (sort-with (lambda (a b) 0) [42]))

;; ============================================================
;; Test 5: Comparator returns zero (stable sort behavior)
;; ============================================================

(print "")
(print "=== Test 5: All Equal Elements ===")

(test-list "sort with all equal comparator result"
  [1 2 3]
  (sort-with (lambda (a b) 0) [1 2 3]))

;; ============================================================
;; Test 6: Sort mixed positive and negative numbers
;; ============================================================

(print "")
(print "=== Test 6: Mixed Positive/Negative ===")

(test-list "sort by absolute value"
  [1 -2 3 -4 5]
  (sort-with (lambda (a b)
                (let ([abs-a (if (< a 0) (* -1 a) a)]
                      [abs-b (if (< b 0) (* -1 b) b)])
                  (if (< abs-a abs-b) -1 (if (> abs-a abs-b) 1 0))))
             [-4 1 3 -2 5]))

;; ============================================================
;; Test 7: Sort array instead of list
;; ============================================================

(print "")
(print "=== Test 7: Array Sort ===")

(test-list "sort array descending"
  [10 8 6 4 2]
  (sort-with (lambda (a b)
                (if (> a b) -1 (if (< a b) 1 0)))
             (array 2 4 6 8 10)))

;; ============================================================
;; Test 8: Custom ordering - odd/even priority
;; ============================================================

(print "")
(print "=== Test 8: Custom Odd/Even Ordering ===")

;; Odd numbers first, then even, both ascending
(test-list "odd numbers first ascending, then even ascending"
  [1 3 5 2 4]
  (sort-with (lambda (a b)
                (let ([odd-a (= 1 (mod a 2))]
                      [odd-b (= 1 (mod b 2))])
                  (if odd-a
                      (if odd-b
                          (if (< a b) -1 (if (> a b) 1 0))
                          -1)  ; odd before even
                      (if odd-b
                          1  ; even after odd
                          (if (< a b) -1 (if (> a b) 1 0))))))
             [5 2 1 4 3]))

;; ============================================================
;; Test 9: Test with NULL input
;; ============================================================

(print "")
(print "=== Test 9: NULL Input ===")

(test-equal "sort with NULL returns nothing"
  1
  (nothing? (sort-with (lambda (a b) 0) nothing)))

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
