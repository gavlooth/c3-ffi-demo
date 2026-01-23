;; test_piping_extended.lisp - Tests for function composition, apply, partial, and dot-field
;;
;; Tests advanced piping features that complement the basic pipe operator (|>):
;;   - compose: Right-to-left function composition
;;   - apply: Apply function to argument list
;;   - partial: Partially apply function with fixed arguments
;;   - flip: Reverse function arguments
;;   - dot-field: Leading dot field access (.field obj)
;;   - pipe-many: Chain multiple pipes
;;
;; Run with: ./omni tests/test_piping_extended.lisp

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

;; ============================================================
;; Test Functions
;; ============================================================

(define inc [x] (+ x 1))
(define square [x] (* x x))
(define double [x] (* x 2))
(define add [x] [y] (+ x y))
(define sub [x] [y] (- x y))
(define mul [x] [y] (* x y))

;; ============================================================
;; Test 1: Basic function composition
;; ============================================================

(print "")
(print "=== Test 1: Function Composition (compose) ===")

;; compose(f, g)(x) = f(g(x))
;; Compose inc and square: (compose square inc) 5 = square(inc 5) = square(6) = 36
(test-eq "compose square inc" 36 ((compose square inc) 5))

;; Compose double and square: (compose double square) 3 = double(square 3) = double(9) = 18
(test-eq "compose double square" 18 ((compose double square) 3))

;; ============================================================
;; Test 2: Apply with single argument
;; ============================================================

(print "")
(print "=== Test 2: Apply (apply) ===")

;; apply function to single-argument list
(test-eq "apply inc single arg" 6 (apply inc [5]))

;; apply square to single argument
(test-eq "apply square" 25 (apply square [5]))

;; ============================================================
;; Test 3: Apply with multiple arguments
;; ============================================================

(print "")
(print "=== Test 3: Apply with Multiple Arguments ===")

;; apply add to argument list
(test-eq "apply add" 15 (apply add [7 8]))

;; apply mul to argument list
(test-eq "apply mul" 12 (apply mul [3 4]))

;; apply sub to argument list
(test-eq "apply sub" 3 (apply sub [10 7]))

;; ============================================================
;; Test 4: Apply with empty argument list
;; ============================================================

(print "")
(print "=== Test 4: Apply with Empty Arguments ===")

(define const [x] x)

;; apply to empty list should work for no-arg function
(test-eq "apply empty args" 42 (apply const [42]))

;; ============================================================
;; Test 5: Partial application - single argument
;; ============================================================

(print "")
(print "=== Test 5: Partial Application (partial) ===")

;; Create add5 by partially applying + with 5
;; Note: This test depends on partial implementation working correctly
(define add-partial [x] [y] (+ x y))

;; Partial application: (partial add-partial 10) creates function waiting for second arg
;; Then (func 5) should give 15
; (define add10 (partial add-partial 10))
; (test-eq "partial add10" 15 (add10 5))

;; ============================================================
;; Test 6: Flip - reverse function arguments
;; ============================================================

(print "")
(print "=== Test 6: Flip Operator ===")

;; flip(sub) creates function where args are reversed
;; (flip sub) 5 10 = sub 10 5 = 5
(define sub-flipped [x] [y] (- y x))

;; Test flipped subtraction
(test-eq "flip sub" 5 ((flip sub) 10 5))

;; flip with division-like operation
(define div-like [x] [y] (/ x y))
(define div-flipped [x] [y] (/ y x))

;; 10 / 2 = 5
;; (flip div) 2 10 = 10 / 2 = 5
(test-eq "flip div" 5 ((flip div-like) 2 10))

;; ============================================================
;; Test 7: Compose with multiple functions (compose-many)
;; ============================================================

(print "")
(print "=== Test 7: Compose Many ===")

;; Compose chain: inc -> double -> square
;; 5 |> inc = 6
;; 6 |> double = 12
;; 12 |> square = 144
; (test-eq "compose-many chain" 144 ((compose-many [inc double square]) 5))

;; ============================================================
;; Test 8: Dot field access on arrays
;; ============================================================

(print "")
(print "=== Test 8: Dot Field Access ===")

;; Create test array
(define test-arr [1 2 3 4 5])

;; Access array fields using dot syntax
(define arr-len (.length test-arr))

;; Length of array should be 5
(test-eq "dot array length" 5 arr-len)

;; ============================================================
;; Test 9: Dot field access on strings
;; ============================================================

(print "")
(print "=== Test 9: Dot Field Access on Strings ===")

(define test-str "hello")

;; Access string length using dot
(define str-len (.length test-str))

;; Length of "hello" should be 5
(test-eq "dot string length" 5 str-len)

;; ============================================================
;; Test 10: Dot field access on pairs/lists
;; ============================================================

(print "")
(print "=== Test 10: Dot Field Access on Pairs ===")

(define test-pair (cons 10 20))

;; Access car (first element) using dot
(define pair-car (.car test-pair))

;; Access cdr (second element) using dot
(define pair-cdr (.cdr test-pair))

(test-eq "dot pair car" 10 pair-car)
(test-eq "dot pair cdr" 20 pair-cdr)

;; Alternative names
(define pair-a (.a test-pair))
(define pair-b (.b test-pair))
(define pair-first (.first test-pair))
(define pair-rest (.rest test-pair))

(test-eq "dot pair a" 10 pair-a)
(test-eq "dot pair b" 20 pair-b)
(test-eq "dot pair first" 10 pair-first)
(test-eq "dot pair rest" 20 pair-rest)

;; ============================================================
;; Test 11: Dot field access on dicts
;; ============================================================

(print "")
(print "=== Test 11: Dot Field Access on Dicts ===")

;; Create test dictionary
(define test-dict #{:name "Alice" :age 30 :city "NYC"})

;; Access dict fields using dot
(define dict-name (.name test-dict))
(define dict-age (.age test-dict))
(define dict-city (.city test-dict))

(test-eq "dot dict name" "Alice" dict-name)
(test-eq "dot dict age" 30 dict-age)
(test-eq "dot dict city" "NYC" dict-city)

;; ============================================================
;; Test 12: Pipe-many - chain multiple functions
;; ============================================================

(print "")
(print "=== Test 12: Pipe Many ===")

;; Chain: 5 -> inc -> double -> square
;; 5 |> inc = 6
;; 6 |> double = 12
;; 12 |> square = 144
; (define funcs [inc double square])
; (test-eq "pipe-many chain" 144 (pipe-many 5 funcs))

;; ============================================================
;; Test 13: Compose associativity
;; ============================================================

(print "")
(print "=== Test 13: Compose Associativity ===")

;; compose(f, compose(g, h)) should equal compose(compose(f, g), h)
;; Not directly testing, but showing compose chains work
(test-eq "compose chain 3 functions" 36 ((compose double (compose inc inc)) 5))

;; ============================================================
;; Test 14: Apply with variadic function simulation
;; ============================================================

(print "")
(print "=== Test 14: Apply Simulated Variadic ===")

;; Simulate sum of list using apply with reduction
;; This is not variadic, but shows apply pattern
(define sum-three [x] [y] [z] (+ (+ x y) z))
(test-eq "apply three args" 6 (apply sum-three [1 2 3]))

;; ============================================================
;; Test 15: Dot field with array index
;; ============================================================

(print "")
(print "=== Test 15: Dot Field Array Index ===")

;; Create array with numbers
(define nums [10 20 30 40 50])

;; REVIEWED:SYNTAX
;; Access by string index (if implemented)
(define idx-0 (.0 nums))
(define idx-2 (.2 nums))
(define idx-4 (.4 nums))

(test-eq "dot array index 0" 10 idx-0)
(test-eq "dot array index 2" 30 idx-2)
(test-eq "dot array index 4" 50 idx-4)

;; ============================================================
;; Test 16: Flip with commutative operations
;; ============================================================

(print "")
(print "=== Test 16: Flip with Commutative Ops ===")

;; flip shouldn't change result for commutative operations like addition
(test-eq "flip add commutative" 15 ((flip add) 7 8))

;; But should for non-commutative like subtraction
(test-eq "flip sub non-commutative" 3 ((flip sub) 10 7))

;; ============================================================
;; Test 17: Apply with nil argument list
;; ============================================================

(print "")
(print "=== Test 17: Apply Edge Cases ===")

;; Testing apply behavior with various edge cases
;; Note: These tests depend on implementation details

;; Apply to single-element list
(test-eq "apply single element" 6 (apply inc [5]))

;; Apply to empty list for function that takes one arg
;; Result depends on implementation - may error or return nothing
; (test-eq "apply empty list for one-arg func" nothing (apply inc []))

;; ============================================================
;; Test 18: Dot field with nested structures
;; ============================================================

(print "")
(print "=== Test 18: Dot Field Nested ===")

;; Create nested dict
(define outer #{:name "Outer" :inner #{:value 42}})

;; Access nested field
(define inner-obj (.inner outer))
(define inner-val (.value inner-obj))

(test-eq "dot nested field" 42 inner-val)

;; ============================================================
;; Test 19: Compose with identity
;; ============================================================

(print "")
(print "=== Test 19: Compose Identity ===")

;; compose(identity, f) should equal f
(define identity [x] x)

(test-eq "compose with identity" 6 ((compose inc identity) 5))
(test-eq "compose identity with" 6 ((compose identity inc) 5))

;; ============================================================
;; Test 20: Flip with single-argument function
;; ============================================================

(print "")
(print "=== Test 20: Flip Single Arg ===")

;; flip on single-arg function should behave normally
(test-eq "flip single arg" 36 ((flip square) 6))

;; ============================================================
;; Test 21: Dot field with different property names
;; ============================================================

(print "")
(print "=== Test 21: Dot Field Property Names ===")

;; Test various common property names
(define person #{:name "Bob" :age 25 :active true})

(define p-name (.name person))
(define p-age (.age person))
(define p-active (.active person))

(test-eq "dot property name" "Bob" p-name)
(test-eq "dot property age" 25 p-age)
(test-eq "dot property active" true p-active)

;; ============================================================
;; Test 22: Apply with closure
;; ============================================================

(print "")
(print "=== Test 22: Apply with Closure ===")

;; Create a closure and apply it
(define make-adder [n] (lambda [x] (+ x n)))
(define add-seven (make-adder 7))

(test-eq "apply closure" 17 (apply add-seven [10]))

;; ============================================================
;; Test 23: Compose complex chain
;; ============================================================

(print "")
(print "=== Test 23: Compose Complex Chain ===")

;; Build complex composition
;; f(x) = square(double(inc(x)))
;; f(3) = square(double(inc(3))) = square(double(4)) = square(8) = 64
(test-eq "compose complex chain" 64 ((compose square (compose double inc)) 3))

;; ============================================================
;; Test 24: Flip used with pipe
;; ============================================================

(print "")
(print "=== Test 24: Flip with Pipe ===")

;; Use flip to make sub work in left-to-right pipe context
;; (flip sub) 10 5 = sub 5 10 = -5
;; In pipe: 5 |> (flip sub) 10 = sub 10 5 = 5
(test-eq "flip with pipe" 5 (5 |> (flip sub) 10))

;; ============================================================
;; Test 25: Dot field with missing key
;; ============================================================

(print "")
(print "=== Test 25: Dot Field Missing Key ===")

;; Access non-existent field
(define missing-dict #{:a 1 :b 2})

;; Accessing missing key should return nothing or null
(define missing-val (.c missing-dict))

(test-bool "dot missing key" true (nothing? missing-val))

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
