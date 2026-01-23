;; test_partition_simple.lisp - Tests for partition primitive
;;
;; Tests prim_partition function which splits a collection into two
;; groups based on a predicate function.
;; Returns an array containing [matches, non-matches].

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

;; ============================================================
;; Basic Partition Tests (Arrays)
;; ============================================================

(print "")
(print "=== Basic Partition Tests (Arrays) ===")

;; Test 1: Partition even/odd numbers
(let ([result (partition (fn [x] (= (mod x 2) 0)) [1 2 3 4 5 6])])
  (let ([matches (array-ref result 0)]
        [non-matches (array-ref result 1)])
    (test-eq "matches count" 3 (array-length matches))
    (test-eq "non-matches count" 3 (array-length non-matches))))

;; Test 2: All elements match
(let ([result (partition (fn [x] (> x 0)) [1 2 3 4 5])])
  (let ([matches (array-ref result 0)]
        [non-matches (array-ref result 1)])
    (test-eq "all match - matches count" 5 (array-length matches))
    (test-eq "all match - non-matches count" 0 (array-length non-matches))))

;; Test 3: No elements match
(let ([result (partition (fn [x] (< x 0)) [1 2 3 4 5])])
  (let ([matches (array-ref result 0)]
        [non-matches (array-ref result 1)])
    (test-eq "none match - matches count" 0 (array-length matches))
    (test-eq "none match - non-matches count" 5 (array-length non-matches))))

;; Test 4: Empty array
(let ([result (partition (fn [x] true) [])])
  (let ([matches (array-ref result 0)]
        [non-matches (array-ref result 1)])
    (test-eq "empty array - matches count" 0 (array-length matches))
    (test-eq "empty array - non-matches count" 0 (array-length non-matches))))

;; ============================================================
;; String Partition Tests
;; ============================================================

(print "")
(print "=== String Partition Tests ===")

;; Test 5: Partition by string length
(let ([result (partition (fn [s] (< (string-length s) 5)) ["a" "hello" "cat" "worldwide" "hi"])])
  (let ([matches (array-ref result 0)]
        [non-matches (array-ref result 1)])
    (test-eq "short strings count" 3 (array-length matches))
    (test-eq "long strings count" 2 (array-length non-matches))))

;; ============================================================
;; List Partition Tests
;; ============================================================

(print "")
(print "=== List Partition Tests ===")

;; Test 6: Partition list by even/odd
(let ([result (partition (fn [x] (= (mod x 2) 0)) '(1 2 3 4 5 6))])
  (let ([matches (array-ref result 0)]
        [non-matches (array-ref result 1)])
    (test-eq "list partition - matches count" 3 (array-length matches))
    (test-eq "list partition - non-matches count" 3 (array-length non-matches))))

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
