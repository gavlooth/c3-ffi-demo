;; test_take_drop.lisp - Tests for take and drop collection primitives
;;
;; Tests take and drop functions which are fundamental sequence operations:
;;   - take n coll: Returns first n elements
;;   - drop n coll: Returns collection without first n elements
;;
;; These functions support both arrays and lists, and handle edge cases
;; like empty collections, zero/negative counts, and taking/dropping
;; more elements than available.
;;
;; Run with: ./omni tests/test_take_drop.lisp

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

(define test-list-equal [name] [expected] [actual]
  (set! test-count (+ test-count 1))
  (if (list-equal expected actual)
      (do
        (set! pass-count (+ pass-count 1))
        (print "PASS:" name))
      (do
        (set! fail-count (+ fail-count 1))
        (print "FAIL:" name)
        (print "  Expected list:" expected)
        (print "  Got:" actual))))

(define test-array-length [name] [expected-length] [actual-arr]
  (set! test-count (+ test-count 1))
  (if (= expected-length (array-length actual-arr))
      (do
        (set! pass-count (+ pass-count 1))
        (print "PASS:" name))
      (do
        (set! fail-count (+ fail-count 1))
        (print "FAIL:" name)
        (print "  Expected length:" expected-length)
        (print "  Got:" (array-length actual-arr)))))

;; ============================================================
;; take Tests - Arrays
;; ============================================================

(print "")
(print "=== take Tests (Arrays) ===")

;; Test 1: Basic take
(let ([result (take 3 [1 2 3 4 5])])
  (test-array-length "take 3 from array" 3 result)
  (if (= (array-length result) 3)
      (do
        (test-equal "first element" 1 (array-ref result 0))
        (test-equal "second element" 2 (array-ref result 1))
        (test-equal "third element" 3 (array-ref result 2)))))

;; Test 2: Take more elements than available
(let ([result (take 10 [1 2 3])])
  (test-array-length "take 10 from 3-element array" 3 result))

;; Test 3: Take all elements
(let ([result (take 5 [1 2 3 4 5])])
  (test-array-length "take all elements" 5 result))

;; Test 4: Take zero elements
(let ([result (take 0 [1 2 3])])
  (test-array-length "take 0 from array" 0 result))

;; Test 5: Take from empty array
(let ([result (take 3 [])])
  (test-array-length "take from empty array" 0 result))

;; Test 6: Take one element
(let ([result (take 1 [42])])
  (test-array-length "take 1 element" 1 result)
  (if (= (array-length result) 1)
      (test-equal "element value" 42 (array-ref result 0))))

;; ============================================================
;; take Tests - Lists
;; ============================================================

(print "")
(print "=== take Tests (Lists) ===")

;; Test 7: Basic take from list
(let ([result (take 3 '(1 2 3 4 5))])
  (test-list-equal "take 3 from list" '(1 2 3) result))

;; Test 8: Take more than available from list
(let ([result (take 10 '(1 2 3))])
  (test-list-equal "take 10 from 3-element list" '(1 2 3) result))

;; Test 9: Take all from list
(let ([result (take 3 '(1 2 3))])
  (test-list-equal "take all from list" '(1 2 3) result))

;; Test 10: Take zero from list
(let ([result (take 0 '(1 2 3))])
  (test-list-equal "take 0 from list" '() result))

;; Test 11: Take from empty list
(let ([result (take 3 '())])
  (test-list-equal "take from empty list" '() result))

;; ============================================================
;; drop Tests - Arrays
;; ============================================================

(print "")
(print "=== drop Tests (Arrays) ===")

;; Test 12: Basic drop
(let ([result (drop 2 [1 2 3 4 5])])
  (test-array-length "drop 2 from array" 3 result)
  (if (= (array-length result) 3)
      (do
        (test-equal "first after drop" 3 (array-ref result 0))
        (test-equal "second after drop" 4 (array-ref result 1))
        (test-equal "third after drop" 5 (array-ref result 2)))))

;; Test 13: Drop all elements
(let ([result (drop 5 [1 2 3 4 5])])
  (test-array-length "drop all elements" 0 result))

;; Test 14: Drop zero elements
(let ([result (drop 0 [1 2 3])])
  (test-array-length "drop 0 from array" 3 result))

;; Test 15: Drop more than available
(let ([result (drop 10 [1 2 3])])
  (test-array-length "drop more than available" 0 result))

;; Test 16: Drop from empty array
(let ([result (drop 3 [])])
  (test-array-length "drop from empty array" 0 result))

;; Test 17: Drop one element
(let ([result (drop 1 [1 2 3])])
  (test-array-length "drop 1 from 3-element array" 2 result)
  (if (= (array-length result) 2)
      (do
        (test-equal "first after drop 1" 2 (array-ref result 0))
        (test-equal "second after drop 1" 3 (array-ref result 1)))))

;; ============================================================
;; drop Tests - Lists
;; ============================================================

(print "")
(print "=== drop Tests (Lists) ===")

;; Test 18: Basic drop from list
(let ([result (drop 2 '(1 2 3 4 5))])
  (test-list-equal "drop 2 from list" '(3 4 5) result))

;; Test 19: Drop all from list
(let ([result (drop 3 '(1 2 3))])
  (test-list-equal "drop all from list" '() result))

;; Test 20: Drop zero from list
(let ([result (drop 0 '(1 2 3))])
  (test-list-equal "drop 0 from list" '(1 2 3) result))

;; Test 21: Drop more than available from list
(let ([result (drop 10 '(1 2 3))])
  (test-list-equal "drop more than available from list" '() result))

;; Test 22: Drop from empty list
(let ([result (drop 3 '())])
  (test-list-equal "drop from empty list" '() result))

;; ============================================================
;; Edge Cases and Type Variations
;; ============================================================

(print "")
(print "=== Edge Cases ===")

;; Test 23: take with strings
(let ([result (take 2 ["hello" "world" "foo"])])
  (test-array-length "take strings" 2 result))

;; Test 24: drop with strings
(let ([result (drop 1 ["hello" "world" "foo"])])
  (test-array-length "drop string" 2 result))

;; Test 25: Composition - drop then take
(let ([result (take 2 (drop 2 [1 2 3 4 5]))])
  (test-array-length "drop 2 then take 2" 2 result)
  (if (= (array-length result) 2)
      (do
        (test-equal "first element" 3 (array-ref result 0))
        (test-equal "second element" 4 (array-ref result 1)))))

;; Test 26: Composition - take then drop
(let ([result (drop 1 (take 3 [1 2 3 4 5]))])
  (test-array-length "take 3 then drop 1" 2 result)
  (if (= (array-length result) 2)
      (do
        (test-equal "first element" 2 (array-ref result 0))
        (test-equal "second element" 3 (array-ref result 1)))))

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
