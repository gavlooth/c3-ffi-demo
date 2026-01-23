;; test_string_trim_left.lisp - Tests for string-trim-left function
;;
;; Tests that string-trim-left removes only leading whitespace
;; from strings while preserving trailing whitespace.
;;
;; Run with: ./omni tests/test_string_trim_left.lisp

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
;; Test 1: Remove leading spaces
;; ============================================================

(print "")
(print "=== Test 1: Basic Leading Space Removal ===")

(define test1 (string-trim-left "  hello"))

(test-string "remove two leading spaces" "hello" test1)

;; ============================================================
;; Test 2: Preserve trailing whitespace
;; ============================================================

(print "")
(print "=== Test 2: Preserve Trailing Whitespace ===")

(define test2 (string-trim-left "  hello  "))

(test-string "preserve trailing spaces" "hello  " test2)

;; ============================================================
;; Test 3: Mix of whitespace characters
;; ============================================================

(print "")
(print "=== Test 3: Mixed Whitespace Characters ===")

(define test3 (string-trim-left "\t\n  \rtest"))

(test-string "remove tabs, newlines, and spaces" "test" test3)

;; ============================================================
;; Test 4: No leading whitespace
;; ============================================================

(print "")
(print "=== Test 4: No Leading Whitespace ===")

(define test4 (string-trim-left "hello"))

(test-string "return unchanged when no leading whitespace" "hello" test4)

;; ============================================================
;; Test 5: Empty string
;; ============================================================

(print "")
(print "=== Test 5: Empty String ===")

(define test5 (string-trim-left ""))

(test-string "handle empty string" "" test5)

;; ============================================================
;; Test 6: Only whitespace
;; ============================================================

(print "")
(print "=== Test 6: Only Whitespace ===")

(define test6 (string-trim-left "   \t\n  "))

(test-string "whitespace-only string becomes empty" "" test6)

;; ============================================================
;; Test 7: Multiple tabs
;; ============================================================

(print "")
(print "=== Test 7: Multiple Tabs ===")

(define test7 (string-trim-left "\t\t\tworld"))

(test-string "remove leading tabs" "world" test7)

;; ============================================================
;; Test 8: Trailing newline preserved
;; ============================================================

(print "")
(print "=== Test 8: Trailing Newline Preserved ===")

(define test8 (string-trim-left "  test\n"))

(test-string "preserve trailing newline" "test\n" test8)

;; ============================================================
;; Test 9: Mixed leading and internal whitespace
;; ============================================================

(print "")
(print "=== Test 9: Internal Whitespace Preserved ===")

(define test9 (string-trim-left "  hello world  "))

(test-string "preserve internal spaces" "hello world  " test9)

;; ============================================================
;; Test 10: String with only trailing whitespace
;; ============================================================

(print "")
(print "=== Test 10: Only Trailing Whitespace ===")

(define test10 (string-trim-left "hello  "))

(test-string "return unchanged when only trailing whitespace" "hello  " test10)

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
