;; test_interleave.lisp - Tests for interleave collection primitive
;; Run with: ./csrc/omnilisp tests/test_interleave.lisp

(println "")
(println "=== Interleave Tests ===")

;; Test 1: Basic interleave
(println "Test 1: Basic interleave [1 2 3] [4 5 6]")
(define result1 (interleave [1 2 3] [4 5 6]))
(println "  Result length:" (array-length result1))

;; Test 2: String arrays
(println "Test 2: String interleave")
(define result2 (interleave ["a" "b"] ["c" "d"]))
(println "  Result length:" (array-length result2))

;; Test 3: Single element
(println "Test 3: Single element")
(define result3 (interleave [1] [2]))
(println "  Result length:" (array-length result3))

;; Test 4: Mismatched lengths
(println "Test 4: Mismatched lengths")
(define result4 (interleave [1 2] [3 4 5 6]))
(println "  Result length:" (array-length result4))

;; Test 5: Empty arrays
(println "Test 5: Empty arrays")
(define result5 (interleave [] []))
(println "  Result length:" (array-length result5))

;; Test 6: Lists
(println "Test 6: List interleave")
(define result6 (interleave '(1 2 3) '(4 5 6)))
(println "  Result length:" (array-length result6))

;; Test 7: Empty lists
(println "Test 7: Empty lists")
(define result7 (interleave '() '()))
(println "  Result length:" (array-length result7))

;; Test 8: Mixed list/array
(println "Test 8: Mixed collections")
(define result8 (interleave '(1 2 3) [4 5 6]))
(println "  Result length:" (array-length result8))

(println "")
(println "=== All Tests Completed Successfully ===")
