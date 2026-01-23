;; test_io_operations.lisp - Tests for I/O operations (read-lines, write-lines, etc.)
;;
;; Tests core I/O functions that are currently untested:
;; - read-lines: Read file as array of lines
;; - write-lines: Write array of lines to file
;; - file-exists?: Check if path exists
;; - file?: Check if path is a regular file
;; - directory?: Check if path is a directory
;; - path-join: Join path components
;;
;; Run with: ./omni tests/test_io_operations.lisp

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

(define test-true [name] [actual]
  (set! test-count (+ test-count 1))
  (if actual
      (do
        (set! pass-count (+ pass-count 1))
        (print "PASS:" name))
      (do
        (set! fail-count (+ fail-count 1))
        (print "FAIL:" name)
        (print "  Expected: true")
        (print "  Got: false"))))

(define test-false [name] [actual]
  (set! test-count (+ test-count 1))
  (if (not actual)
      (do
        (set! pass-count (+ pass-count 1))
        (print "PASS:" name))
      (do
        (set! fail-count (+ fail-count 1))
        (print "FAIL:" name)
        (print "  Expected: false")
        (print "  Got: true"))))

;; ============================================================
;; Test Helper Functions
;; ============================================================

;; Create a temporary file path (Unix-specific)
(define temp-file-count 0)

(define make-temp-file-name []
  (set! temp-file-count (+ temp-file-count 1))
  (string-append "/tmp/omnilisp_io_test_" (string-append temp-file-count ".txt")))

;; Clean up temporary files
(define temp-files (list))

(define add-temp-file [path]
  (set! temp-files (cons path temp-files)))

(define cleanup-temp-files []
  (for-each (lambda (path)
              (when (file-exists? path)
                (begin (ignore (delete-file path) nothing)))
            temp-files)
  (set! temp-files (list)))

;; ============================================================
;; Test 1: read-lines function
;; ============================================================

(print "")
(print "=== Test 1: read-lines Function ===")

;; Test 1.1: Read basic file with multiple lines
(do
  (define path (make-temp-file-name))
  (add-temp-file path)
  (write-file path "Line 1\nLine 2\nLine 3\n")
  (define lines (read-lines path))
  (test-eq "read-lines returns array" true (array? lines))
  (test-eq "read-lines has correct count" 3 (array-length lines))
  (cleanup-temp-files))

;; Test 1.2: Read empty file
(do
  (define path (make-temp-file-name))
  (add-temp-file path)
  (write-file path "")
  (define lines (read-lines path))
  (test-eq "read-lines on empty file" 0 (array-length lines))
  (cleanup-temp-files))

;; Test 1.3: Read single line without newline
(do
  (define path (make-temp-file-name))
  (add-temp-file path)
  (write-file path "Single line")
  (define lines (read-lines path))
  (test-eq "read-lines single line" 1 (array-length lines))
  (cleanup-temp-files))

;; Test 1.4: Read file with trailing newline
(do
  (define path (make-temp-file-name))
  (add-temp-file path)
  (write-file path "Line 1\nLine 2\n")
  (define lines (read-lines path))
  (test-eq "read-lines with trailing newline" 2 (array-length lines))
  (cleanup-temp-files))

;; ============================================================
;; Test 2: write-lines function
;; ============================================================

(print "")
(print "=== Test 2: write-lines Function ===")

;; Test 2.1: Write array of lines
(do
  (define path (make-temp-file-name))
  (add-temp-file path)
  (define lines (array "Line 1" "Line 2" "Line 3"))
  (write-lines path lines)
  (define read-lines-back (read-lines path))
  (test-eq "write-lines and read-lines" 3 (array-length read-lines-back))
  (cleanup-temp-files))

;; Test 2.2: Write empty array
(do
  (define path (make-temp-file-name))
  (add-temp-file path)
  (define lines (array))
  (write-lines path lines)
  (define read-lines-back (read-lines path))
  (test-eq "write-lines empty array" 0 (array-length read-lines-back))
  (cleanup-temp-files))

;; ============================================================
;; Test 3: file-exists? predicate
;; ============================================================

(print "")
(print "=== Test 3: file-exists? Predicate ===")

;; Test 3.1: Check existing file
(do
  (define path (make-temp-file-name))
  (add-temp-file path)
  (write-file path "content")
  (test-true "file-exists? on existing file" (file-exists? path))
  (cleanup-temp-files))

;; Test 3.2: Check non-existent file
(do
  (define path "/tmp/omnilisp_nonexistent_xyz123.txt")
  (test-false "file-exists? on non-existent file" (file-exists? path)))

;; ============================================================
;; Test 4: file? predicate
;; ============================================================

(print "")
(print "=== Test 4: file? Predicate ===")

;; Test 4.1: Check regular file
(do
  (define path (make-temp-file-name))
  (add-temp-file path)
  (write-file path "content")
  (test-true "file? on regular file" (file? path))
  (cleanup-temp-files))

;; Test 4.2: Check directory (should be false)
(do
  (define path "/tmp")
  (test-false "file? on directory" (file? path)))

;; ============================================================
;; Test 5: directory? predicate
;; ============================================================

(print "")
(print "=== Test 5: directory? Predicate ===")

;; Test 5.1: Check directory
(do
  (define path "/tmp")
  (test-true "directory? on /tmp" (directory? path)))

;; Test 5.2: Check regular file (should be false)
(do
  (define path (make-temp-file-name))
  (add-temp-file path)
  (write-file path "content")
  (test-false "directory? on file" (directory? path))
  (cleanup-temp-files))

;; ============================================================
;; Test 6: path-join function
;; ============================================================

(print "")
(print "=== Test 6: path-join Function ===")

;; Test 6.1: Join two paths
(do
  (define parts (array "/home" "user"))
  (define result (path-join parts))
  (test-equal "path-join basic" "/home/user" result))

;; Test 6.2: Join multiple paths
(do
  (define parts (array "/var" "lib" "omnilisp" "module.ol"))
  (define result (path-join parts))
  (test-equal "path-join multiple" "/var/lib/omnilisp/module.ol" result))

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

;; Clean up any remaining temp files
(cleanup-temp-files)

;; Return count of failures (0 = success)
fail-count
