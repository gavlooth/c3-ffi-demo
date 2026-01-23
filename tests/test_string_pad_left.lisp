;; test_string_pad_left.lisp - Tests for string-pad-left primitive
;;
;; Tests string-pad-left function which pads a string on the left side
;; with a specified character to reach a target width.
;; This is important for formatting output and aligning text columns.
;;
;; Run with: ./omni tests/test_string_pad_left.lisp

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
;; Test 1: Basic left padding with spaces (default)
;; ============================================================

(print "")
(print "=== Test 1: Basic Left Padding ===")

(test-string "pad left with spaces (width > length)"
  "   hello"
  (string-pad-left "hello" 8))

(test-string "pad left with spaces (exact width)"
  "hello"
  (string-pad-left "hello" 5))

(test-string "pad left with spaces (no padding needed)"
  "hello"
  (string-pad-left "hello" 3))

(test-string "pad left with many spaces"
  "          test"
  (string-pad-left "test" 14))

;; ============================================================
;; Test 2: Custom padding characters
;; ============================================================

(print "")
(print "=== Test 2: Custom Padding Characters ===")

(test-string "pad left with zeros"
  "0000123"
  (string-pad-left "123" 7 "0"))

(test-string "pad left with hyphens"
  "---text"
  (string-pad-left "text" 7 "-"))

(test-string "pad left with asterisks"
  "***data"
  (string-pad-left "data" 7 "*"))

(test-string "pad left with underscore"
  "____value"
  (string-pad-left "value" 9 "_"))

;; ============================================================
;; Test 3: Edge cases - empty strings
;; ============================================================

(print "")
(print "=== Test 3: Empty Strings ===")

(test-string "pad empty string with spaces"
  "     "
  (string-pad-left "" 5))

(test-string "pad empty string with zeros"
  "00000"
  (string-pad-left "" 5 "0"))

(test-string "pad empty string with width 0"
  ""
  (string-pad-left "" 0))

;; ============================================================
;; Test 4: Edge cases - single characters
;; ============================================================

(print "")
(print "=== Test 4: Single Character Strings ===")

(test-string "pad single character with spaces"
  "    a"
  (string-pad-left "a" 5))

(test-string "pad single character with custom char"
  "****x"
  (string-pad-left "x" 5 "*"))

(test-string "no padding for single char (width = 1)"
  "a"
  (string-pad-left "a" 1))

;; ============================================================
;; Test 5: Negative and zero width
;; ============================================================

(print "")
(print "=== Test 5: Negative and Zero Width ===")

(test-string "zero width (returns original)"
  "hello"
  (string-pad-left "hello" 0))

;; Negative width is treated as 0
(test-string "negative width (treated as 0)"
  "hello"
  (string-pad-left "hello" -5))

;; ============================================================
;; Test 6: Strings with special characters
;; ============================================================

(print "")
(print "=== Test 6: Special Characters ===")

(test-string "pad string with newlines"
  "  \n"
  (string-pad-left "\n" 3))

(test-string "pad string with tabs"
  "  \t"
  (string-pad-left "\t" 3))

(test-string "pad string with spaces"
  "     hello world"
  (string-pad-left "hello world" 16))

;; ============================================================
;; Test 7: Large widths
;; ============================================================

(print "")
(print "=== Test 7: Large Widths ===")

(test-string "large width padding"
  "                    x"
  (string-pad-left "x" 20))

(test-string "very large width"
  "                                                                                                                    test"
  (string-pad-left "test" 128))

;; ============================================================
;; Test 8: Multibyte characters
;; ============================================================

(print "")
(print "=== Test 8: Multibyte Characters ===")

(test-string "pad string with unicode"
  "   café"
  (string-pad-left "café" 6))

(test-string "pad string with emoji (may vary by encoding)"
  "  😀"
  (string-pad-left "😀" 4))

;; ============================================================
;; Test 9: Padding string with only spaces
;; ============================================================

(print "")
(print "=== Test 9: Padding Whitespace Strings ===")

(test-string "pad space string"
  "  "
  (string-pad-left " " 2))

(test-string "pad multiple spaces"
  "    "
  (string-pad-left "  " 4))

;; ============================================================
;; Test 10: Consistency with other padding operations
;; ============================================================

(print "")
(print "=== Test 10: Consistency Checks ===")

;; Verify that padding left then right gives expected total width
(define padded-left (string-pad-left "test" 10 "0"))
(define padded-both (string-pad-right padded-left 15 "*"))
(test-string "left pad then right pad (total width)"
  "00000test*****"
  padded-both)

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
