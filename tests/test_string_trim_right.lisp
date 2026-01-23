;; test_string_trim_right.lisp - Tests for string-trim-right function
;;
;; Tests that string-trim-right removes only trailing whitespace
;; from strings while preserving leading whitespace.
;;
;; Run with: ./omni tests/test_string_trim_right.lisp

;; ============================================================
;; Test Framework
;; ============================================================

(define test-count 0)
(define pass-count 0)
(define fail-count 0)

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
;; Test 1: Remove trailing spaces
;; ============================================================

(print "")
(print "=== Test 1: Basic Trailing Space Removal ===")

(define test1 (string-trim-right "hello  "))

(test-string "remove two trailing spaces" "hello" test1)

;; ============================================================
;; Test 2: Preserve leading whitespace
;; ============================================================

(print "")
(print "=== Test 2: Preserve Leading Whitespace ===")

(define test2 (string-trim-right "  hello  "))

(test-string "preserve leading spaces" "  hello" test2)

;; ============================================================
;; Test 3: Mix of whitespace characters
;; ============================================================

(print "")
(print "=== Test 3: Mixed Whitespace Characters ===")

(define test3 (string-trim-right "test\t\n  \r"))

(test-string "remove tabs, newlines, spaces, and carriage returns" "test" test3)

;; ============================================================
;; Test 4: No trailing whitespace
;; ============================================================

(print "")
(print "=== Test 4: No Trailing Whitespace ===")

(define test4 (string-trim-right "hello"))

(test-string "return unchanged when no trailing whitespace" "hello" test4)

;; ============================================================
;; Test 5: Empty string
;; ============================================================

(print "")
(print "=== Test 5: Empty String ===")

(define test5 (string-trim-right ""))

(test-string "handle empty string" "" test5)

;; ============================================================
;; Test 6: Only whitespace
;; ============================================================

(print "")
(print "=== Test 6: Only Whitespace ===")

(define test6 (string-trim-right "   \t\n  "))

(test-string "whitespace-only string becomes empty" "" test6)

;; ============================================================
;; Test 7: Multiple tabs
;; ============================================================

(print "")
(print "=== Test 7: Multiple Trailing Tabs ===")

(define test7 (string-trim-right "world\t\t\t"))

(test-string "remove trailing tabs" "world" test7)

;; ============================================================
;; Test 8: Leading newline preserved
;; ============================================================

(print "")
(print "=== Test 8: Leading Characters Preserved ===")

(define test8 (string-trim-right "\ntest  "))

(test-string "preserve leading newline" "\ntest" test8)

;; ============================================================
;; Test 9: Mixed internal and trailing whitespace
;; ============================================================

(print "")
(print "=== Test 9: Internal Whitespace Preserved ===")

(define test9 (string-trim-right "  hello world  "))

(test-string "preserve internal and leading spaces" "  hello world" test9)

;; ============================================================
;; Test 10: String with only leading whitespace
;; ============================================================

(print "")
(print "=== Test 10: Only Leading Whitespace ===")

(define test10 (string-trim-right "  hello"))

(test-string "return unchanged when only leading whitespace" "  hello" test10)

;; ============================================================
;; Test 11: Single trailing space
;; ============================================================

(print "")
(print "=== Test 11: Single Trailing Space ===")

(define test11 (string-trim-right "test "))

(test-string "remove single trailing space" "test" test11)

;; ============================================================
;; Test 12: Carriage return
;; ============================================================

(print "")
(print "=== Test 12: Trailing Carriage Return ===")

(define test12 (string-trim-right "data\r"))

(test-string "remove trailing carriage return" "data" test12)

;; ============================================================
;; Test 13: Combined trailing whitespace
;; ============================================================

(print "")
(print "=== Test 13: Combined Trailing Whitespace ===")

(define test13 (string-trim-right "value \t\r\n"))

(test-string "remove combined trailing whitespace" "value" test13)

;; ============================================================
;; Test 14: String with content and no whitespace
;; ============================================================

(print "")
(print "=== Test 14: No Whitespace ===")

(define test14 (string-trim-right "OmniLisp"))

(test-string "return unchanged for string with no whitespace" "OmniLisp" test14)

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
