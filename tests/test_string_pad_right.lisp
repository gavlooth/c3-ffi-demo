;; test_string_pad_right.lisp - Tests for string-pad-right primitive
;;
;; Tests string-pad-right function which pads a string on the right side
;; with a specified character to reach a target width.
;; This is important for formatting output, aligning text columns, and
;; creating consistent-width fields in data display.
;;
;; Run with: ./omni tests/test_string_pad_right.lisp

;; ============================================================
;; Test Framework
;; ============================================================

(define test-count 0)
(define pass-count 0)
(define fail-count 0)

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
;; Test 1: Basic right padding with spaces (default)
;; ============================================================

(print "")
(print "=== Test 1: Basic Right Padding ===")

(test-string "pad right with spaces (width > length)"
  "hello   "
  (string-pad-right "hello" 8))

(test-string "pad right with spaces (exact width)"
  "hello"
  (string-pad-right "hello" 5))

(test-string "pad right with spaces (no padding needed)"
  "hello"
  (string-pad-right "hello" 3))

(test-string "pad right with many spaces"
  "test          "
  (string-pad-right "test" 14))

;; ============================================================
;; Test 2: Custom padding characters
;; ============================================================

(print "")
(print "=== Test 2: Custom Padding Characters ===")

(test-string "pad right with zeros"
  "1230000"
  (string-pad-right "123" 7 "0"))

(test-string "pad right with hyphens"
  "text---"
  (string-pad-right "text" 7 "-"))

(test-string "pad right with asterisks"
  "data***"
  (string-pad-right "data" 7 "*"))

(test-string "pad right with underscore"
  "value____"
  (string-pad-right "value" 9 "_"))

(test-string "pad right with dots"
  "end...."
  (string-pad-right "end" 7 "."))

;; ============================================================
;; Test 3: Edge cases - empty strings
;; ============================================================

(print "")
(print "=== Test 3: Empty Strings ===")

(test-string "pad empty string with spaces"
  "     "
  (string-pad-right "" 5))

(test-string "pad empty string with zeros"
  "00000"
  (string-pad-right "" 5 "0"))

(test-string "pad empty string with width 0"
  ""
  (string-pad-right "" 0))

(test-string "pad empty string with custom char"
  "&&&&&"
  (string-pad-right "" 5 "&"))

;; ============================================================
;; Test 4: Edge cases - single characters
;; ============================================================

(print "")
(print "=== Test 4: Single Character Strings ===")

(test-string "pad single character with spaces"
  "a    "
  (string-pad-right "a" 5))

(test-string "pad single character with custom char"
  "x****"
  (string-pad-right "x" 5 "*"))

(test-string "no padding for single char (width = 1)"
  "a"
  (string-pad-right "a" 1))

(test-string "pad single character to large width"
  "#                  "
  (string-pad-right "#" 20))

;; ============================================================
;; Test 5: Negative and zero width
;; ============================================================

(print "")
(print "=== Test 5: Negative and Zero Width ===")

(test-string "zero width (returns original)"
  "hello"
  (string-pad-right "hello" 0))

;; Negative width is treated as 0
(test-string "negative width (treated as 0)"
  "hello"
  (string-pad-right "hello" -5))

(test-string "negative width with custom pad (treated as 0)"
  "test"
  (string-pad-right "test" -10 "#"))

;; ============================================================
;; Test 6: Strings with special characters
;; ============================================================

(print "")
(print "=== Test 6: Special Characters ===")

(test-string "pad string with newlines"
  "\n  "
  (string-pad-right "\n" 3))

(test-string "pad string with tabs"
  "\t  "
  (string-pad-right "\t" 3))

(test-string "pad string with spaces"
  "hello world     "
  (string-pad-right "hello world" 16))

(test-string "pad string with mixed special chars"
  "a@b%c  "
  (string-pad-right "a@b%c" 7))

;; ============================================================
;; Test 7: Large widths
;; ============================================================

(print "")
(print "=== Test 7: Large Widths ===")

(test-string "large width padding"
  "x                    "
  (string-pad-right "x" 20))

(test-string "very large width"
  "test                                                                                                                    "
  (string-pad-right "test" 128))

(test-string "moderate large width"
  "data                        "
  (string-pad-right "data" 30))

;; ============================================================
;; Test 8: Multibyte characters
;; ============================================================

(print "")
(print "=== Test 8: Multibyte Characters ===")

(test-string "pad string with unicode"
  "café  "
  (string-pad-right "café" 6))

(test-string "pad string with emoji (may vary by encoding)"
  "😀  "
  (string-pad-right "😀" 4))

(test-string "pad string with mixed unicode"
  "héllo世 "
  (string-pad-right "héllo世" 8))

;; ============================================================
;; Test 9: Padding string with only spaces
;; ============================================================

(print "")
(print "=== Test 9: Padding Whitespace Strings ===")

(test-string "pad space string"
  "  "
  (string-pad-right " " 2))

(test-string "pad multiple spaces"
  "    "
  (string-pad-right "  " 4))

(test-string "pad space with custom char"
  " *"
  (string-pad-right " " 2 "*"))

;; ============================================================
;; Test 10: Consistency with other padding operations
;; ============================================================

(print "")
(print "=== Test 10: Consistency Checks ===")

;; Verify that padding right then left gives expected total width
(define padded-right (string-pad-right "test" 10 "0"))
(define padded-both (string-pad-left padded-right 15 "*"))
(test-string "right pad then left pad (total width)"
  "*****test00000"
  padded-both)

;; Verify multiple padding operations
(define triple-pad (string-pad-right "x" 5 "*"))
(define triple-pad-final (string-pad-right triple-pad 10 "-"))
(test-string "multiple right padding operations"
  "x****-----"
  triple-pad-final)

;; ============================================================
;; Test 11: Padding with different character types
;; ============================================================

(print "")
(print "=== Test 11: Different Character Types ===")

(test-string "pad with numbers"
  "value111"
  (string-pad-right "value" 8 "1"))

(test-string "pad with letters"
  "abcXX"
  (string-pad-right "abc" 5 "X"))

(test-string "pad with brackets"
  "test[]"
  (string-pad-right "test" 6 "["))

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
