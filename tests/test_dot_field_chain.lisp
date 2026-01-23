;; test_dot_field_chain.lisp - Tests for dot-field-chain primitive
;;
;; Tests prim_dot_field_chain function which chains multiple field accesses.
;; This enables compact syntax for accessing deeply nested fields:
;;   (dot-field-chain obj ['field1 'field2 'field3])
;; is equivalent to:
;;   (.field3 (.field2 (.field1 obj)))
;;
;; Run with: ./omni tests/test_dot_field_chain.lisp

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
  (if (= expected actual)
      (do
        (set! pass-count (+ pass-count 1))
        (print "PASS:" name))
      (do
        (set! fail-count (+ fail-count 1))
        (print "FAIL:" name)
        (print "  Expected:" expected)
        (print "  Got:" actual))))

(define test-string [name] [expected] [actual]
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
;; Test 1: Chain through nested dictionaries
;; ============================================================

(print "")
(print "=== Test 1: Nested Dictionary Chains ===")

;; Create nested dictionary structure
;; #{:a #{:b #{:c 42}}}
(define nested-dict #{:a #{:b #{:c 42}}})

;; Chain through three levels of nesting
;; Should access nested-dict -> :a -> :b -> :c -> 42
(define result1 (dot-field-chain nested-dict ['a 'b 'c]))
(test-eq "chain three nested dicts" 42 result1)

;; ============================================================
;; Test 2: Chain through mixed structures (dict + array + dict)
;; ============================================================

(print "")
(print "=== Test 2: Mixed Structure Chains ===")

;; Create structure mixing dicts and arrays
;; #{:data [#(:value "hello")]}
; REVIEWED:SYNTAX
(define mixed-struct #{:data [#(:value "hello")]})

;; Chain: dict -> array index 0 -> dict -> :value
;; Should return "hello"
(define result2 (dot-field-chain mixed-struct ['data "0" 'value]))
(test-string "chain dict-array-dict" "hello" result2)

;; ============================================================
;; Test 3: Chain through pairs (using car/cdr aliases)
;; ============================================================

(print "")
(print "=== Test 3: Pair/Cons Chains ===")

;; Create nested pair structure: (cons (cons (cons 1 2) 3) 4)
;; Which is: (1, 2), (1,2) -> 3, ((1,2),3) -> 4
;; In memory: [[1, 2], 3], 4
(define nested-pair (cons (cons (cons 1 2) 3) 4))

;; Chain: car (first) -> car (first) -> car (first)
;; Should get 1
(define result3 (dot-field-chain nested-pair ['first 'first 'first]))
(test-eq "chain through nested pairs" 1 result3)

;; Chain: car (first) -> car (first) -> cdr (rest)
;; Should get 2
(define result4 (dot-field-chain nested-pair ['first 'first 'rest']))
(test-eq "chain to second nested element" 2 result4)

;; ============================================================
;; Test 4: Chain through array indices as strings
;; ============================================================

(print "")
(print "=== Test 4: Array Index Chains ===")

;; Create array of arrays
;; [[10 20] [30 40]]
(define nested-array [[10 20] [30 40]])

;; Chain: index 0 -> index 1
;; Should get 20
(define result5 (dot-field-chain nested-array ["0" "1"]))
(test-eq "chain through array indices" 20 result5)

;; ============================================================
;; Test 5: Access string length through chain
;; ============================================================

(print "")
(print "=== Test 5: Property Access Chains ===")

;; Create dict containing string
;; #{:message "hello"}
(define string-dict #{:message "hello"})

;; Chain: dict -> :message -> length
;; Should get 5
(define result6 (dot-field-chain string-dict ['message 'length]))
(test-eq "chain to string length property" 5 result6)

;; ============================================================
;; Test 6: Deep chain (5+ levels)
;; ============================================================

(print "")
(print "=== Test 6: Deep Chains ===")

;; Create deeply nested structure
;; #{:l1 #{:l2 #{:l3 #{:l4 #{:l5 "deep"}}}}}
(define deep-nest #{:l1 #{:l2 #{:l3 #{:l4 #{:l5 "deep"}}}}})

;; Chain through 5 levels
(define result7 (dot-field-chain deep-nest ['l1 'l2 'l3 'l4 'l5]))
(test-string "chain through 5 nesting levels" "deep" result7)

;; ============================================================
;; Test 7: Chain with missing field
;; ============================================================

(print "")
(print "=== Test 7: Error Handling ===")

;; Create simple dict
;; #{:a 1}
(define simple-dict #{:a 1})

;; Chain trying to access non-existent field
;; Should return nothing (null/missing value)
(define result8 (dot-field-chain simple-dict ['a 'missing]))
(test-bool "chain with missing field returns nothing" true (nothing? result8))

;; ============================================================
;; Test 8: Single-level chain (degenerate case)
;; ============================================================

(print "")
(print "=== Test 8: Degenerate Cases ===")

;; Single field access (chain of length 1)
(define single-level #{:value 100})
(define result9 (dot-field-chain single-level ['value]))
(test-eq "single-level chain" 100 result9)

;; ============================================================
;; Test 9: Chain with empty field list
;; ============================================================

;; Empty chain should return original object
(define result10 (dot-field-chain single-level []))
(test-eq "empty chain returns original" 100 result10)

;; ============================================================
;; Test 10: Real-world JSON-like structure
;; ============================================================

(print "")
(print "=== Test 10: Real-World Data ===")

;; Simulate JSON response from API
;; #{
;;   :user #{
;;     :profile #{
;;       :name "Alice"
;;       :settings #{
;;         :notifications true
;;       }
;;     }
;;   }
;; }
(define api-response #{:user #{:profile #{:name "Alice" :settings #{:notifications true}}}})

;; Chain to access nested setting
(define result11 (dot-field-chain api-response ['user 'profile 'settings 'notifications]))
(test-bool "chain to deeply nested boolean" true result11)

;; Chain to access name
(define result12 (dot-field-chain api-response ['user 'profile 'name]))
(test-string "chain to nested string value" "Alice" result12)

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
