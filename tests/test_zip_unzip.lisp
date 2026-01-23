;; test_zip_unzip.lisp - Comprehensive tests for zip and unzip collection primitives
;;
;; Tests zip and unzip functions which are fundamental pairing operations:
;;   - zip coll1 coll2: Pairs elements from two collections into array of pairs
;;   - unzip coll: Unpairs an array of pairs into two arrays
;;
;; These functions support both arrays and lists, handle edge cases
;; like empty collections, mismatched lengths (zip uses shorter), and
;; preserve types correctly.
;;
;; Run with: ./omni tests/test_zip_unzip.lisp

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

(define test-array-equal [name] [expected] [actual]
  (set! test-count (+ test-count 1))
  (if (equal? expected actual)
      (do
        (set! pass-count (+ pass-count 1))
        (print "PASS:" name))
      (do
        (set! fail-count (+ fail-count 1))
        (print "FAIL:" name)
        (print "  Expected array:" expected)
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

(define test-list-equal [name] [expected] [actual]
  (set! test-count (+ test-count 1))
  (if (equal? expected actual)
      (do
        (set! pass-count (+ pass-count 1))
        (print "PASS:" name))
      (do
        (set! fail-count (+ fail-count 1))
        (print "FAIL:" name)
        (print "  Expected list:" expected)
        (print "  Got:" actual))))

;; ============================================================
;; zip Tests - Arrays (Equal Lengths)
;; ============================================================

(print "")
(print "=== zip Tests (Arrays - Equal Lengths) ===")

;; Test 1: Basic zip with integer arrays
(let ([result (zip [1 2 3] [4 5 6])])
  (test-array-length "zip equal length arrays" 3 result)
  (if (= (array-length result) 3)
      (do
        (test-equal "first pair first" 1 (array-ref (array-ref result 0) 0))
        (test-equal "first pair second" 4 (array-ref (array-ref result 0) 1))
        (test-equal "second pair first" 2 (array-ref (array-ref result 1) 0))
        (test-equal "second pair second" 5 (array-ref (array-ref result 1) 1))
        (test-equal "third pair first" 3 (array-ref (array-ref result 2) 0))
        (test-equal "third pair second" 6 (array-ref (array-ref result 2) 1)))))

;; Test 2: Zip with string arrays
(let ([result (zip ["a" "b"] ["c" "d"])])
  (test-array-length "zip string arrays" 2 result)
  (if (= (array-length result) 2)
      (do
        (test-equal "string first pair" "a" (array-ref (array-ref result 0) 0))
        (test-equal "string second pair" "c" (array-ref (array-ref result 0) 1)))))

;; Test 3: Zip with mixed type arrays
(let ([result (zip [1 "two" 3.0] [4 5 true])])
  (test-array-length "zip mixed type arrays" 3 result))

;; ============================================================
;; zip Tests - Arrays (Mismatched Lengths)
;; ============================================================

(print "")
(print "=== zip Tests (Arrays - Mismatched Lengths) ===")

;; Test 4: Zip shorter first array
(let ([result (zip [1 2] [3 4 5 6])])
  (test-array-length "zip shorter first array" 2 result)
  (if (= (array-length result) 2)
      (do
        (test-equal "first pair" 1 (array-ref (array-ref result 0) 0))
        (test-equal "second pair" 2 (array-ref (array-ref result 1) 0)))))

;; Test 5: Zip shorter second array
(let ([result (zip [1 2 3 4] [5 6])])
  (test-array-length "zip shorter second array" 2 result)
  (if (= (array-length result) 2)
      (do
        (test-equal "first pair" 5 (array-ref (array-ref result 0) 1))
        (test-equal "second pair" 6 (array-ref (array-ref result 1) 1)))))

;; Test 6: Zip with single element arrays
(let ([result (zip [1] [2])])
  (test-array-length "zip single element arrays" 1 result)
  (if (= (array-length result) 1)
      (do
        (test-equal "single pair first" 1 (array-ref (array-ref result 0) 0))
        (test-equal "single pair second" 2 (array-ref (array-ref result 0) 1)))))

;; ============================================================
;; zip Tests - Empty Arrays
;; ============================================================

(print "")
(print "=== zip Tests (Empty Arrays) ===")

;; Test 7: Zip empty arrays
(let ([result (zip [] [])])
  (test-array-length "zip two empty arrays" 0 result))

;; Test 8: Zip one empty array
(let ([result (zip [] [1 2 3])])
  (test-array-length "zip first empty array" 0 result))

;; Test 9: Zip other empty array
(let ([result (zip [1 2 3] [])])
  (test-array-length "zip second empty array" 0 result))

;; ============================================================
;; zip Tests - Lists
;; ============================================================

(print "")
(print "=== zip Tests (Lists) ===")

;; Test 10: Basic zip with lists
(let ([result (zip '(1 2 3) '(4 5 6))])
  (test-array-length "zip equal length lists" 3 result))

;; Test 11: Zip lists with mismatched lengths
(let ([result (zip '(1 2) '(3 4 5 6))])
  (test-array-length "zip shorter first list" 2 result))

;; Test 12: Zip empty lists
(let ([result (zip '() '())])
  (test-array-length "zip empty lists" 0 result))

;; Test 13: Zip list with array
(let ([result (zip '(1 2 3) [4 5 6])])
  (test-array-length "zip list and array" 3 result))

;; Test 14: Zip array with list
(let ([result (zip [1 2 3] '(4 5 6))])
  (test-array-length "zip array and list" 3 result))

;; ============================================================
;; unzip Tests
;; ============================================================

(print "")
(print "=== unzip Tests ===")

;; Test 15: Basic unzip array of pairs
(let ([result (unzip [[1 4] [2 5] [3 6]])])
  (test-array-length "unzip returns 2 arrays" 2 result)
  (if (= (array-length result) 2)
      (do
        (let ([firsts (array-ref result 0)]
              [seconds (array-ref result 1)])
          (test-array-length "firsts array" 3 firsts)
          (test-array-length "seconds array" 3 seconds)
          (if (= (array-length firsts) 3)
              (do
                (test-equal "first first" 1 (array-ref firsts 0))
                (test-equal "second first" 2 (array-ref firsts 1))
                (test-equal "third first" 3 (array-ref firsts 2))))
          (if (= (array-length seconds) 3)
              (do
                (test-equal "first second" 4 (array-ref seconds 0))
                (test-equal "second second" 5 (array-ref seconds 1))
                (test-equal "third second" 6 (array-ref seconds 2)))))))

;; Test 16: Unzip with strings
(let ([result (unzip [["a" "c"] ["b" "d"]])])
  (test-array-length "unzip string pairs" 2 result)
  (if (= (array-length result) 2)
      (do
        (let ([firsts (array-ref result 0)]
              [seconds (array-ref result 1)])
          (test-equal "first string" "a" (array-ref firsts 0))
          (test-equal "second string" "c" (array-ref firsts 1))
          (test-equal "third string" "b" (array-ref seconds 0))
          (test-equal "fourth string" "d" (array-ref seconds 1))))))

;; Test 17: Unzip empty array
(let ([result (unzip [])])
  (test-array-length "unzip empty array" 2 result)
  (if (= (array-length result) 2)
      (do
        (test-array-length "empty firsts" 0 (array-ref result 0))
        (test-array-length "empty seconds" 0 (array-ref result 1)))))

;; Test 18: Unzip single pair
(let ([result (unzip [[1 2]])])
  (test-array-length "unzip single pair" 2 result)
  (if (= (array-length result) 2)
      (do
        (test-array-length "single firsts" 1 (array-ref result 0))
        (test-array-length "single seconds" 1 (array-ref result 1)))))

;; ============================================================
;; zip/unzip Round-trip Tests
;; ============================================================

(print "")
(print "=== Round-trip Tests ===")

;; Test 19: Zip then unzip returns original arrays
(let ([arr1 [1 2 3]]
      [arr2 [4 5 6]]
      [zipped (zip arr1 arr2)]
      [unzipped (unzip zipped)])
  (test-array-equal "round-trip first array" arr1 (array-ref unzipped 0))
  (test-array-equal "round-trip second array" arr2 (array-ref unzipped 1)))

;; Test 20: Unzip then zip returns original pairs
(let ([pairs [[1 4] [2 5] [3 6]]]
      [unzipped (unzip pairs)]
      [re-zipped (zip (array-ref unzipped 0) (array-ref unzipped 1))])
  (test-array-equal "round-trip pairs" pairs re-zipped))

;; ============================================================
;; Edge Cases and Type Preservation
;; ============================================================

(print "")
(print "=== Edge Cases ===")

;; Test 21: Zip with nothings
(let ([result (zip [nothing nothing] [1 2])])
  (test-array-length "zip with nothing" 2 result))

;; Test 22: Zip with booleans
(let ([result (zip [true false true] [false true false])])
  (test-array-length "zip booleans" 3 result))

;; Test 23: Zip with floats
(let ([result (zip [1.5 2.5 3.5] [4.5 5.5 6.5])])
  (test-array-length "zip floats" 3 result))

;; Test 24: Large zip
(let ([result (zip (collect-array (range 100)) (collect-array (range 100 200)))])
  (test-array-length "zip large arrays" 100 result))

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
