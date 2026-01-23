;; test_string_concat.lisp - Tests for string-concat primitive
;;
;; Tests string-concat function which concatenates two strings.
;; This is a fundamental operation for building strings and combining
;; text data, which is used extensively throughout the language.
;;
;; Run with: ./omni tests/test_string_concat.lisp

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
;; Test 1: Basic concatenation
;; ============================================================

(print "")
(print "=== Test 1: Basic Concatenation ===")

(test-string "concat two simple strings"
  "hello world"
  (string-concat "hello " "world"))

(test-string "concat without space"
  "helloworld"
  (string-concat "hello" "world"))

(test-string "concat numbers as strings"
  "123456"
  (string-concat "123" "456"))

;; ============================================================
;; Test 2: Empty strings
;; ============================================================

(print "")
(print "=== Test 2: Empty Strings ===")

(test-string "first string empty"
  "world"
  (string-concat "" "world"))

(test-string "second string empty"
  "hello"
  (string-concat "hello" ""))

(test-string "both strings empty"
  ""
  (string-concat "" ""))

;; ============================================================
;; Test 3: Single character strings
;; ============================================================

(print "")
(print "=== Test 3: Single Character Strings ===")

(test-string "concat single characters"
  "ab"
  (string-concat "a" "b"))

(test-string "concat single with empty"
  "a"
  (string-concat "a" ""))

;; ============================================================
;; Test 4: Special characters
;; ============================================================

(print "")
(print "=== Test 4: Special Characters ===")

(test-string "concat with newline"
  "hello\nworld"
  (string-concat "hello\n" "world"))

(test-string "concat with tab"
  "hello\tworld"
  (string-concat "hello\t" "world"))

(test-string "concat spaces"
  "hello  world"
  (string-concat "hello  " "world"))

(test-string "concat with punctuation"
  "hello, world!"
  (string-concat "hello, " "world!"))

;; ============================================================
;; Test 5: Multibyte characters (unicode)
;; ============================================================

(print "")
(print "=== Test 5: Unicode Characters ===")

(test-string "concat with unicode"
  "café"
  (string-concat "caf" "é"))

(test-string "concat emoji"
  "😀😊"
  (string-concat "😀" "😊"))

(test-string "mixed unicode and ascii"
  "hello世界"
  (string-concat "hello" "世界"))

;; ============================================================
;; Test 6: String building pattern
;; ============================================================

(print "")
(print "=== Test 6: String Building ===")

;; Test sequential concatenation
(define result1 "hello")
(set! result1 (string-concat result1 " "))
(set! result1 (string-concat result1 "world"))
(test-string "build string incrementally"
  "hello world"
  result1)

;; Test creating path-like strings
(define path "/home")
(set! path (string-concat path "/user"))
(set! path (string-concat path "/docs"))
(test-string "build path-like string"
  "/home/user/docs"
  path)

;; ============================================================
;; Test 7: Large strings
;; ============================================================

(print "")
(print "=== Test 7: Large Strings ===")

(define large-a "aaaaaaaaaaaaaaaaaaaa")  ;; 20 chars
(define large-b "bbbbbbbbbbbbbbbbbbbb")  ;; 20 chars
(test-string "concat large strings"
  "aaaaaaaaaaaaaaaaaaaabbbbbbbbbbbbbbbbbbbb"
  (string-concat large-a large-b))

;; ============================================================
;; Test 8: Strings with quotes and escapes
;; ============================================================

(print "")
(print "=== Test 8: Quotes and Escapes ===")

(test-string "concat with double quotes"
  "\"hello\" \"world\""
  (string-concat "\"hello\"" " \"world\""))

(test-string "concat with backslashes"
  "path\\to\\file"
  (string-concat "path\\to" "\\file"))

;; ============================================================
;; Test 9: Repeated concatenation
;; ============================================================

(print "")
(print "=== Test 9: Repeated Concatenation ===")

(define repeated "")
(set! repeated (string-concat repeated "a"))
(set! repeated (string-concat repeated "b"))
(set! repeated (string-concat repeated "c"))
(test-string "concat three times sequentially"
  "abc"
  repeated)

;; ============================================================
;; Test 10: Idempotency checks
;; ============================================================

(print "")
(print "=== Test 10: Idempotency ===")

;; Concatenating with empty string should return same value
(define original "test")
(define after-concat (string-concat original ""))
(test-string "concat with empty (right side)"
  "test"
  after-concat)

(define before-concat (string-concat "" "test"))
(test-string "concat with empty (left side)"
  "test"
  before-concat)

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
