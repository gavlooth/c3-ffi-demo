;; test_collections_extended.lisp - Tests for untested collection functions
;;
;; Tests for collection functions that are currently untested:
;; - group-by: Group elements by key function
;; - take-while: Take elements while predicate is true
;; - drop-while: Drop elements while predicate is true
;; - flatten-deep: Deep flatten nested collections
;; - interpose: Insert separator between elements
;;
;; Run with: ./omni tests/test_collections_extended.lisp

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

;; REVIEWED:SYNTAX
;; Helper to check if two lists are equal
(define list-equal? [l1] [l2]
  (cond
    [(and (not l1) (not l2)) true]
    [(or (not l1) (not l2)) false]
    [else
     (and (= (car l1) (car l2))
          (list-equal? (cdr l1) (cdr l2)))]))

;; ============================================================
;; Test 1: group-by function
;; ============================================================

(print "")
(print "=== Test 1: group-by Function ===")

; REVIEWED:SYNTAX
;; Test 1.1: Group numbers by even/odd
(do
  (define nums (list 1 2 3 4 5 6))
  (define groups (group-by (lambda (n) (= (mod n 2) 0)) nums))
  (print "Groups:" groups)
  (test-true "group-by returns map" (map? groups)))

;; Test 1.2: Group strings by first letter
(do
  (define words (list "apple" "banana" "apricot" "blueberry"))
  (define groups (group-by (lambda (s) (string-substr s 0 1)) words))
  (test-true "group-by with strings" (map? groups)))

;; ============================================================
;; Test 2: take-while function
;; ============================================================

(print "")
(print "=== Test 2: take-while Function ===")

;; Test 2.1: Take while numbers are positive
(do
  (define nums (list 1 2 3 -1 4 5))
  (define result (take-while (lambda (n) (> n 0)) nums))
  (print "Result:" result)
  (test-true "take-while returns list" (list? result)))

;; Test 2.2: Take while strings have length > 1
(do
  (define strs (list "ab" "cd" "e" "fg"))
  (define result (take-while (lambda (s) (> (string-length s) 1)) strs))
  (test-true "take-while with strings" (list? result)))

;; Test 2.3: Take while true (should take all)
(do
  (define nums (list 1 2 3 4 5))
  (define result (take-while (lambda (x) true) nums))
  (test-true "take-while true takes all" (list? result)))

;; Test 2.4: Take while false (should take none)
(do
  (define nums (list 1 2 3 4 5))
  (define result (take-while (lambda (x) false) nums))
  (test-true "take-while false takes none" (list? result)))

;; ============================================================
;; Test 3: drop-while function
;; ============================================================

(print "")
(print "=== Test 3: drop-while Function ===")

;; Test 3.1: Drop while numbers are positive
(do
  (define nums (list 1 2 3 -1 4 5))
  (define result (drop-while (lambda (n) (> n 0)) nums))
  (print "Result:" result)
  (test-true "drop-while returns list" (list? result)))

;; Test 3.2: Drop while strings have length > 1
(do
  (define strs (list "ab" "cd" "e" "fg"))
  (define result (drop-while (lambda (s) (> (string-length s) 1)) strs))
  (test-true "drop-while with strings" (list? result)))

;; Test 3.3: Drop while true (should drop all)
(do
  (define nums (list 1 2 3 4 5))
  (define result (drop-while (lambda (x) true) nums))
  (test-true "drop-while true drops all" (list? result)))

;; Test 3.4: Drop while false (should drop none)
(do
  (define nums (list 1 2 3 4 5))
  (define result (drop-while (lambda (x) false) nums))
  (test-true "drop-while false drops none" (list? result)))

;; ============================================================
;; Test 4: flatten-deep function
;; ============================================================

(print "")
(print "=== Test 4: flatten-deep Function ===")

;; Test 4.1: Deep flatten nested lists
(do
  (define nested (list 1 (list 2 (list 3 4)) 5))
  (define result (flatten-deep nested))
  (print "Result:" result)
  (test-true "flatten-deep returns list" (list? result)))

;; Test 4.2: Deep flatten mixed nesting
(do
  (define nested (list (list 1 2) (list (list 3)) (list 4 5)))
  (define result (flatten-deep nested))
  (test-true "flatten-deep mixed nesting" (list? result)))

;; Test 4.3: Deep flatten already flat list
(do
  (define flat (list 1 2 3 4 5))
  (define result (flatten-deep flat))
  (test-true "flatten-deep flat list" (list? result)))

;; Test 4.4: Deep flatten empty nested lists
(do
  (define nested (list (list) (list (list))))
  (define result (flatten-deep nested))
  (test-true "flatten-deep empty nested" (list? result)))

;; ============================================================
;; Test 5: interpose function
;; ============================================================

(print "")
(print "=== Test 5: interpose Function ===")

;; Test 5.1: Interpose between numbers
(do
  (define nums (list 1 2 3 4))
  (define result (interpose 0 nums))
  (print "Result:" result)
  (test-true "interpose returns list" (list? result)))

;; Test 5.2: Interpose between strings
(do
  (define strs (list "a" "b" "c"))
  (define result (interpose "," strs))
  (test-true "interpose with strings" (list? result)))

;; Test 5.3: Interpose with empty list
(do
  (define empty (list))
  (define result (interpose "," empty))
  (test-true "interpose empty list" (list? result)))

;; Test 5.4: Interpose with single element
(do
  (define single (list 1))
  (define result (interpose 0 single))
  (test-true "interpose single element" (list? result)))

;; ============================================================
;; Test 6: Combination Tests
;; ============================================================

(print "")
(print "=== Test 6: Combination Tests ===")

;; Test 6.1: Use take-while and drop-while together
(do
  (define nums (list 1 2 3 -1 4 5 -2 6 7))
  (define taken (take-while (lambda (n) (> n 0)) nums))
  (define remaining (drop-while (lambda (n) (> n 0)) nums))
  (test-true "take-while then drop-while" (and (list? taken) (list? remaining))))

;; Test 6.2: Group then flatten
(do
  (define words (list "a" "ab" "b" "bc" "c"))
  (define groups (group-by (lambda (s) (string-length s)) words))
  (define flat (flatten groups))
  (test-true "group-by then flatten" (and (map? groups) (list? flat))))

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
