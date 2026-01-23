;; test_partition.omni - Comprehensive tests for partition primitive
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
  (if (equal? expected actual)
      (do
        (set! pass-count (+ pass-count 1))
        (print "PASS:" name))
      (do
        (set! fail-count (+ fail-count 1))
        (print "FAIL:" name)
        (print "  Expected:" expected)
        (print "  Got:" actual))))

; REVIEWED:SYNTAX
(define arrays-equal? [arr1] [arr2]
  (if (and arr1 arr2)
      (let ([len1 (array-length arr1)]
            [len2 (array-length arr2)])
        (if (= len1 len2)
            (let ([eq true])
              (let ^:seq
                [i 0]
                [loop (while (< i len1)
                        (do
                          (if (not (equal? (array-ref arr1 i)
                                            (array-ref arr2 i)))
                              (set! eq false))
                          (set! i (+ i 1))))]
                eq))
            false))
      (and (not arr1) (not arr2))))

;; Helper to compare partition results
(define test-partition [name] [pred] [coll] [expected-matches] [expected-non-matches]
  (set! test-count (+ test-count 1))
  (let ([result (partition pred coll)])
    (if (and result
             (>= (array-length result) 2))
        (let ([actual-matches (array-ref result 0)]
              [actual-non-matches (array-ref result 1)])
          (if (and (arrays-equal? expected-matches actual-matches)
                   (arrays-equal? expected-non-matches actual-non-matches))
              (do
                (set! pass-count (+ pass-count 1))
                (print "PASS:" name))
              (do
                (set! fail-count (+ fail-count 1))
                (print "FAIL:" name)
                (print "  Expected matches:" expected-matches)
                (print "  Got matches:" actual-matches)
                (print "  Expected non-matches:" expected-non-matches)
                (print "  Got non-matches:" actual-non-matches))))
        (do
          (set! fail-count (+ fail-count 1))
          (print "FAIL:" name " - Invalid result format")
          (print "  Got:" result)))))

;; ============================================================
;; Basic Partition Tests (Arrays)
;; ============================================================

(print "")
(print "=== Basic Partition Tests (Arrays) ===")

;; Test 1: Partition even/odd numbers
(test-partition "even/odd numbers"
               (fn [x] (= (mod x 2) 0))
               [1 2 3 4 5 6 7 8 9 10]
               [2 4 6 8 10]
               [1 3 5 7 9])

;; Test 2: Partition positive/negative numbers
(test-partition "positive/negative numbers"
               (fn [x] (> x 0))
               [-5 -3 -1 0 1 3 5]
               [1 3 5]
               [-5 -3 -1 0])

;; Test 3: Partition by threshold
(test-partition "greater than 5"
               (fn [x] (> x 5))
               [1 3 5 7 9 11]
               [7 9 11]
               [1 3 5])

;; Test 4: All elements match
(test-partition "all match"
               (fn [x] (> x 0))
               [1 2 3 4 5]
               [1 2 3 4 5]
               [])

;; Test 5: No elements match
(test-partition "none match"
               (fn [x] (< x 0))
               [1 2 3 4 5]
               []
               [1 2 3 4 5])

;; ============================================================
;; String Partition Tests
;; ============================================================

(print "")
(print "=== String Partition Tests ===")

;; Test 6: Partition by string length
(test-partition "short vs long strings (length < 5)"
               (fn [s] (< (string-length s) 5))
               ["a" "hello" "cat" "worldwide" "hi"]
               ["a" "cat" "hi"]
               ["hello" "worldwide"])

;; Test 7: Partition strings containing substring
(test-partition "strings containing 'test'"
               (fn [s] (string-contains s "test"))
               ["test1" "hello" "test2" "world"]
               ["test1" "test2"]
               ["hello" "world"])

;; Test 8: Partition by starting character
(test-partition "strings starting with 'a'"
               (fn [s] (string-starts-with s "a"))
               ["apple" "banana" "apricot" "cherry"]
               ["apple" "apricot"]
               ["banana" "cherry"])

;; ============================================================
;; List Partition Tests
;; ============================================================

(print "")
(print "=== List Partition Tests ===")

;; Test 9: Partition list by even/odd
(test-partition "list even/odd"
               (fn [x] (= (mod x 2) 0))
               '(1 2 3 4 5 6)
               [2 4 6]
               [1 3 5])

;; Test 10: Partition list with mixed types
(test-partition "list numbers vs strings"
               (fn [x] (= (type-of x) {Int}))
               '(1 "hello" 2 "world" 3)
               [1 2 3]
               ["hello" "world"])

;; ============================================================
;; Edge Cases
;; ============================================================

(print "")
(print "=== Edge Cases ===")

;; Test 11: Empty array
(test-partition "empty array"
               (fn [x] true)
               []
               []
               [])

;; Test 12: Empty list
(test-partition "empty list"
               (fn [x] true)
               '()
               []
               [])

;; Test 13: Single element matching
(test-partition "single element (match)"
               (fn [x] (> x 0))
               [5]
               [5]
               [])

;; Test 14: Single element not matching
(test-partition "single element (no match)"
               (fn [x] (< x 0))
               [5]
               []
               [5])

;; Test 15: Predicate returning false for all
(test-partition "predicate always false"
               (fn [x] false)
               [1 2 3 4 5]
               []
               [1 2 3 4 5])

;; Test 16: Predicate returning true for all
(test-partition "predicate always true"
               (fn [x] true)
               [1 2 3 4 5]
               [1 2 3 4 5]
               [])

;; ============================================================
;; Complex Predicate Tests
;; ============================================================

(print "")
(print "=== Complex Predicate Tests ===")

;; Test 17: Multiple conditions in predicate
(test-partition "numbers divisible by 2 and 3"
               (fn [x] (and (= (mod x 2) 0)
                             (= (mod x 3) 0)))
               [1 2 3 4 5 6 7 8 9 10 11 12]
               [6 12]
               [1 2 3 4 5 7 8 9 10 11])

;; Test 18: Predicate with complex logic
(test-partition "strings with uppercase first letter"
               (fn [s] (= (string-upcase (string-substr s 0 1))
                          (string-substr s 0 1)))
               ["apple" "Banana" "cherry" "Date"]
               ["Banana" "Date"]
               ["apple" "cherry"])

;; ============================================================
;; Partition with Boolean Values
;; ============================================================

(print "")
(print "=== Boolean Partition Tests ===")

;; Test 19: Partition actual booleans
(test-partition "true vs false"
               (fn [x] x)
               [true false true false true]
               [true true true]
               [false false])

;; Test 20: Partition based on boolean result
(test-partition "numeric truthiness"
               (fn [x] (not (= x 0)))
               [0 1 0 2 0 3]
               [1 2 3]
               [0 0 0])

;; ============================================================
;; Nested Collection Tests
;; ============================================================

(print "")
(print "=== Nested Collection Tests ===")

;; Test 21: Partition arrays of arrays (by inner array length)
(test-partition "inner arrays with length > 2"
               (fn [arr] (> (array-length arr) 2))
               [[1] [1 2] [1 2 3] [1 2 3 4]]
               [[1 2 3] [1 2 3 4]]
               [[1] [1 2]])

;; ============================================================
;; Performance / Stress Tests
;; ============================================================

(print "")
(print "=== Performance Tests ===")

;; Test 22: Large array partition
; REVIEWED:SYNTAX
(define large-array (array))
(let ^:seq [i 0]
  (while (< i 100)
    (do
      (array-push! large-array i)
      (set! i (+ i 1)))))
(test-partition "large array partition (100 elements)"
               (fn [x] (= (mod x 2) 0))
               large-array
               [0 2 4 6 8 10 12 14 16 18 20 22 24 26 28 30 32 34 36 38 40 42 44 46 48 50 52 54 56 58 60 62 64 66 68 70 72 74 76 78 80 82 84 86 88 90 92 94 96 98]
               [1 3 5 7 9 11 13 15 17 19 21 23 25 27 29 31 33 35 37 39 41 43 45 47 49 51 53 55 57 59 61 63 65 67 69 71 73 75 77 79 81 83 85 87 89 91 93 95 97 99])

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
