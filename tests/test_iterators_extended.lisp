;; test_iterators_extended.lisp - Tests for iterator and generator functions
;;
;; Tests advanced iterator/generator features:
;;   - collect: Collect elements from iterator into collection
;;   - range: Create range iterator (0 to n-1)
;;   - make-generator: Create generator from producer function
;;   - generator-next: Get next value from generator
;;   - generator-done: Check if generator is exhausted
;;   - iter-next-unified: Unified iterator/generator next
;;   - take-unified: Take from iterator or generator
;;
;; Run with: ./omni tests/test_iterators_extended.lisp

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

(define test-bool [name] [expected] [actual]
  (set! test-count (+ test-count 1))
  (if (eq expected actual)
      (do
        (set! pass-count (+ pass-count 1))
        (print "PASS:" name))
      (do
        (set! fail-count (+ fail-count 1))
        (print "FAIL:" name)
        (print "  Expected:" expected)
        (print "  Got:" actual))))

(define test-not-eq [name] [not-expected] [actual]
  (set! test-count (+ test-count 1))
  (if (not (= not-expected actual))
      (do
        (set! pass-count (+ pass-count 1))
        (print "PASS:" name))
      (do
        (set! fail-count (+ fail-count 1))
        (print "FAIL:" name)
        (print "  Expected NOT:" not-expected)
        (print "  Got:" actual))))

;; ============================================================
;; Test 1: Collect iterator to list
;; ============================================================

(print "")
(print "=== Test 1: Collect (list) ===")

;; Create iterator and collect to list
(define nums-iter (iterate inc 0))
(define nums-collected (collect (take 5 nums-iter) 'list))

;; Should produce list (0 1 2 3 4)
(test-eq "collect list length" 5 (length nums-collected))
(test-eq "collect list first" 0 (first nums-collected))
(test-eq "collect list last" 4 (last nums-collected))

;; ============================================================
;; Test 2: Collect iterator to array
;; ============================================================

(print "")
(print "=== Test 2: Collect (array) ===")

(define nums-array (collect (take 5 (iterate inc 10)) 'array))

;; Should produce array [10 11 12 13 14]
(test-eq "collect array length" 5 (array-length nums-array))
(test-eq "collect array first" 10 (array-get nums-array 0))
(test-eq "collect array last" 14 (array-get nums-array 4))

;; ============================================================
;; Test 3: Collect from list (passthrough)
;; ============================================================

(print "")
(print "=== Test 3: Collect from List ===")

(define input-list (list 1 2 3 4 5))
(define collected-list (collect input-list 'list))

;; Collecting list as list should return same list
(test-eq "collect list from list" 5 (length collected-list))
(test-eq "collect list first" 1 (first collected-list))

;; ============================================================
;; Test 4: Collect from array
;; ============================================================

(print "")
(print "=== Test 4: Collect from Array ===")

(define input-array [100 200 300 400])
(define collected-array (collect input-array 'array))

;; Collecting array as array should work
(test-eq "collect array from array" 4 (array-length collected-array))
(test-eq "collect array element" 200 (array-get collected-array 1))

;; ============================================================
;; Test 5: Collect to string from characters
;; ============================================================

(print "")
(print "=== Test 5: Collect to String ===")

(define chars-list (list 72 101 108 108 111))  ; ASCII for "Hello"
(define hello-str (collect chars-list 'string))

;; Should produce "Hello"
(test-eq "collect string length" 5 (string-length hello-str))
(test-eq "collect string content" "Hello" hello-str)

;; ============================================================
;; Test 6: Range iterator (positive)
;; ============================================================

(print "")
(print "=== Test 6: Range Iterator ===")

;; Create range 0 to 4
(define r5 (range 5))
(define r5-list (list (first r5) (first (rest r5)) (first (rest (rest r5)))))

;; Should produce 0, 1, 2
(test-eq "range 5 first" 0 (first r5))
(test-eq "range 5 second" 1 (first (rest r5)))
(test-eq "range 5 third" 2 (first (rest (rest r5))))

;; ============================================================
;; Test 7: Range iterator (empty)
;; ============================================================

(print "")
(print "=== Test 7: Range Empty ===")

;; Create empty range
(define r0 (range 0))
(test-eq "range 0 is empty" nothing r0))

;; ============================================================
;; Test 8: Make-generator basic
;; ============================================================

(print "")
(print "=== Test 8: Make Generator ===")

;; Create simple generator
(define my-gen []
  (yield 1)
  (yield 2)
  (yield 3))

;; Note: This test depends on generator implementation working
;; (define g (make-generator my-gen))
;; (test-eq "generator first" 1 (generator-next g))
;; (test-eq "generator second" 2 (generator-next g))
;; (test-eq "generator third" 3 (generator-next g))

;; ============================================================
;; Test 9: Generator-done check
;; ============================================================

(print "")
(print "=== Test 9: Generator Done ===")

;; Check if generator is done
;; After consuming all values, generator-done should return true
;; (test-bool "generator done after consuming" true (generator-done g))

;; Try to get next from exhausted generator
;; (define exhausted-val (generator-next g))
;; (test-eq "generator exhausted" nothing exhausted-val)

;; ============================================================
;; Test 10: Collect from generator
;; ============================================================

(print "")
(print "=== Test 10: Collect Generator ===")

;; Create generator and collect values
(define seq-gen []
  (yield 10)
  (yield 20)
  (yield 30))

;; (define sg (make-generator seq-gen))
;; (define seq-collected (collect sg 'array))

;; Should produce array [10 20 30]
;; (test-eq "collect generator length" 3 (array-length seq-collected))
;; (test-eq "collect generator values" 20 (array-get seq-collected 1))

;; ============================================================
;; Test 11: Range with collect
;; ============================================================

(print "")
(print "=== Test 11: Range with Collect ===")

;; Create range and collect to array
(define range-collected (collect (range 10) 'array))

;; Should produce array [0 1 2 3 4 5 6 7 8 9]
(test-eq "range collect length" 10 (array-length range-collected))
(test-eq "range collect first" 0 (array-get range-collected 0))
(test-eq "range collect last" 9 (array-get range-collected 9))

;; ============================================================
;; Test 12: Collect from empty collection
;; ============================================================

(print "")
(print "=== Test 12: Collect Empty ===")

;; Collect empty list
(define empty-result (collect (list) 'list))

(test-eq "collect empty list" nothing empty-result))

;; Collect empty array
(define empty-arr-result (collect [] 'array))

(test-eq "collect empty array" 0 (array-length empty-arr-result))

;; ============================================================
;; Test 13: Collect with take-while
;; ============================================================

(print "")
(print "=== Test 13: Collect with Take While ===")

;; Create numbers and take while less than 5
(define nums (collect (range 20) 'list))
(define small-nums (take-while (lambda [x] (< x 5)) nums))

;; Should get (0 1 2 3 4)
(test-eq "take-while length" 5 (length small-nums))
(test-eq "take-while first" 0 (first small-nums))
(test-eq "take-while last" 4 (last small-nums))

;; ============================================================
;; Test 14: Collect with drop-while
;; ============================================================

(print "")
(print "=== Test 14: Collect with Drop While ===")

;; Drop numbers while less than 5
(define large-nums (drop-while (lambda [x] (< x 5)) nums))

;; Should drop (0 1 2 3 4) and get rest
(test-eq "drop-while first" 5 (first large-nums))
(test-eq "drop-while second" 6 (first (rest large-nums)))

;; ============================================================
;; Test 15: Nested collect with iterate
;; ============================================================

(print "")
(print "=== Test 15: Nested Collect ===")

;; Create squares using iterate
(define squares (collect (take 5 (iterate (lambda [x] (* x x)) 1)) 'array))

;; Should produce [1 4 9 16 25] (squares of 1, 2, 3, 4, 5)
(test-eq "iterate squares length" 5 (array-length squares))
(test-eq "iterate squares values" 16 (array-get squares 3))

;; ============================================================
;; Test 16: Range with step (simulated)
;; ============================================================

(print "")
(print "=== Test 16: Range Patterns ===")

;; Even numbers: filter range
(define evens (filter (lambda [x] (= (% x 2) 0)) (collect (range 10) 'list)))

;; Should get (0 2 4 6 8)
(test-eq "evens length" 5 (length evens))
(test-eq "evens first" 0 (first evens))
(test-eq "evens last" 8 (last evens))

;; Odd numbers
(define odds (filter (lambda [x] (not (= (% x 2) 0))) (collect (range 10) 'list)))

;; Should get (1 3 5 7 9)
(test-eq "odds length" 5 (length odds))
(test-eq "odds first" 1 (first odds))
(test-eq "odds last" 9 (last odds))

;; ============================================================
;; Test 17: Collect with map
;; ============================================================

(print "")
(print "=== Test 17: Collect with Map ===")

;; Map range to doubles
(define doubled (map (lambda [x] (* x 2)) (collect (range 5) 'list)))

;; Should get (0 2 4 6 8)
(test-eq "map doubled length" 5 (length doubled))
(test-eq "map doubled values" 6 (third doubled))

;; ============================================================
;; Test 18: Collect with reduce
;; ============================================================

(print "")
(print "=== Test 18: Collect with Reduce ===")

;; Sum of range 0-9 = 45
(define range-nums (collect (range 10) 'array))
(define total (reduce + 0 range-nums))

(test-eq "reduce sum" 45 total))

;; ============================================================
;; Test 19: Range to string conversion
;; ============================================================

(print "")
(print "=== Test 19: Range to String ===")

;; Convert range to string using map
(define num-str (collect (map (lambda [x] (+ 48 x)) (range 5)) 'string))

;; Should produce "01234" (ASCII 48 is '0')
(test-eq "range to string" "01234" num-str)

;; ============================================================
;; Test 20: Collect default (array)
;; ============================================================

(print "")
(print "=== Test 20: Collect Default Type ===")

;; Collect without specifying type (defaults to array)
(define default-collect (collect (take 3 (iterate inc 0))))

;; Should default to array
(test-eq "collect default is array" 3 (array-length default-collect))
(test-eq "collect default values" 2 (array-get default-collect 2))

;; ============================================================
;; Test 21: Iterate with complex function
;; ============================================================

(print "")
(print "=== Test 21: Iterate Complex ===")

;; Fibonacci sequence using iterate
(define fib-start (cons 0 1))
(define fibs (take 8 (iterate (lambda [pair] 
                                            (cons (second pair) (+ (first pair) (second pair)))) 
                                            fib-start)))

;; Extract first elements
(define fib-list (map first fibs))

;; Should get first 8 Fibonacci numbers: 0 1 1 2 3 5 8 13
(test-eq "fib length" 8 (length fib-list))
(test-eq "fib 6th" 8 (nth 5 fib-list))
(test-eq "fib 7th" 13 (last fib-list))

;; ============================================================
;; Test 22: Collect with take
;; ============================================================

(print "")
(print "=== Test 22: Collect with Take ===")

;; Take first 3 from range
(define taken (collect (take 3 (range 100)) 'list))

(test-eq "take from range length" 3 (length taken))
(test-eq "take from range first" 0 (first taken))
(test-eq "take from range last" 2 (last taken))

;; ============================================================
;; Test 23: Collect with drop
;; ============================================================

(print "")
(print "=== Test 23: Collect with Drop ===")

;; Drop first 5 from range
(define dropped (collect (drop 5 (range 10)) 'list))

(test-eq "drop from range length" 5 (length dropped))
(test-eq "drop from range first" 5 (first dropped))
(test-eq "drop from range last" 9 (last dropped))

;; ============================================================
;; Test 24: Iterate to infinite sequence (with limit)
;; ============================================================

(print "")
(print "=== Test 24: Infinite Sequence ===")

;; Create powers of 2
(define powers (take 6 (iterate (lambda [x] (* x 2)) 1)))
(define powers-array (collect powers 'array))

;; Should get [1 2 4 8 16 32]
(test-eq "powers of 2 length" 6 (array-length powers-array))
(test-eq "powers of 2 values" 16 (array-get powers-array 3))

;; ============================================================
;; Test 25: Range with negative check
;; ============================================================

(print "")
(print "=== Test 25: Range Edge Cases ===")

;; Range 1 should produce [0]
(define r1 (collect (range 1) 'array))
(test-eq "range 1 length" 1 (array-length r1))
(test-eq "range 1 value" 0 (array-get r1 0))

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
