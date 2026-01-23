;; test_string_trim.lisp - Tests for string-trim primitive
;;
;; Tests string-trim function which removes whitespace from both ends.
;; This is important for data cleaning and user input normalization.
;;
;; Run with: ./omni tests/test_string_trim.lisp

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
;; Test 1: Basic trimming
;; ============================================================

(print "")
(print "=== Test 1: Basic Trimming ===")

(test-string "trim leading spaces"
  "hello"
  (string-trim "  hello"))

(test-string "trim trailing spaces"
  "hello"
  (string-trim "hello  "))

(test-string "trim both ends"
  "hello"
  (string-trim "  hello  "))

(test-string "trim tabs and spaces"
  "world"
  (string-trim "\t\tworld\t\t"))

;; ============================================================
;; Test 2: Empty and all whitespace strings
;; ============================================================

(print "")
(print "=== Test 2: Empty and Whitespace Strings ===")

(test-string "trim empty string"
  ""
  (string-trim ""))

(test-string "trim all spaces"
  ""
  (string-trim "     "))

(test-string "trim mixed whitespace"
  ""
  (string-trim " \t\n\r "))

;; ============================================================
;; Test 3: No trimming needed
;; ============================================================

(print "")
(print "=== Test 3: No Trimming Needed ===")

(test-string "no whitespace to trim"
  "hello world"
  (string-trim "hello world"))

(test-string "single word no whitespace"
  "test"
  (string-trim "test"))

;; ============================================================
;; Test 4: Internal whitespace preserved
;; ============================================================

(print "")
(print "=== Test 4: Internal Whitespace Preserved ===")

(test-string "internal spaces preserved"
  "hello  world"
  (string-trim "  hello  world  "))

(test-string "mixed internal whitespace"
  "a \t b \n c"
  (string-trim " \t a \t b \n c \r "))

;; ============================================================
;; Test 5: Edge cases
;; ============================================================

(print "")
(print "=== Test 5: Edge Cases ===")

(test-string "single space"
  ""
  (string-trim " "))

(test-string "single tab"
  ""
  (string-trim "\t"))

(test-string "newline and spaces"
  "content"
  (string-trim "\n content \r\n"))

(test-string "unicode-like chars preserved"
  "café"
  (string-trim "  café  "))

;; ============================================================
;; Test 6: Multiple whitespace characters
;; ============================================================

(print "")
(print "=== Test 6: Multiple Whitespace Characters ===")

(test-string "many leading spaces"
  "text"
  (string-trim "        text"))

(test-string "many trailing spaces"
  "text"
  (string-trim "text        "))

(test-string "mixed leading whitespace"
  "data"
  (string-trim " \t \r \n data"))

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
