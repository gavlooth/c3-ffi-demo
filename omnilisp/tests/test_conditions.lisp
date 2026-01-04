;; test_conditions.lisp - Comprehensive tests for OmniLisp Condition System
;;
;; Run with: ./omni tests/test_conditions.lisp

;; ============================================================
;; Test Framework
;; ============================================================

(define test-count 0)
(define pass-count 0)
(define fail-count 0)

(define (test-num name expected actual)
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

(define (test-nil name value)
  (set! test-count (+ test-count 1))
  (if (null? value)
      (do
        (set! pass-count (+ pass-count 1))
        (print "PASS:" name))
      (do
        (set! fail-count (+ fail-count 1))
        (print "FAIL:" name "(expected nil)"))))

(define (test-not-nil name value)
  (set! test-count (+ test-count 1))
  (if (null? value)
      (do
        (set! fail-count (+ fail-count 1))
        (print "FAIL:" name "(expected non-nil)"))
      (do
        (set! pass-count (+ pass-count 1))
        (print "PASS:" name))))

;; ============================================================
;; Basic handler-case Tests
;; ============================================================

(print "")
(print "=== handler-case Tests ===")

;; Test 1: handler-case with no error
(test-num "handler-case no error"
  42
  (handler-case
    (+ 40 2)
    (error (e) 999)))

;; Test 2: handler-case catches signal
(test-num "handler-case catches signal"
  100
  (handler-case
    (signal (quote error) "boom")
    (error (e) 100)))

;; Test 3: handler-case multiple clauses - first match
(test-num "handler-case type match"
  200
  (handler-case
    (signal (quote type-error) "type mismatch")
    (error (e) 100)
    (type-error (e) 200)))

;; Test 4: handler-case returns expression value
(test-num "handler-case returns expr value"
  100
  (handler-case
    (* 10 10)
    (error (e) 0)))

;; ============================================================
;; restart-case Tests
;; ============================================================

(print "")
(print "=== restart-case Tests ===")

;; Test 5: restart-case with no restart invoked
(test-num "restart-case no invocation"
  30
  (restart-case
    (+ 10 20)
    (use-value (v) v)))

;; Test 6: restart-case with invoke-restart
(test-num "restart-case with invoke"
  42
  (restart-case
    (invoke-restart (quote use-value) 42)
    (use-value (v) v)))

;; Test 7: restart-case abort pattern
(test-nil "restart abort returns nil"
  (restart-case
    (invoke-restart (quote abort))
    (abort () ())))

;; Test 8: restart-case with computed value
(test-num "restart with computation"
  100
  (restart-case
    (invoke-restart (quote double) 50)
    (double (x) (* x 2))))

;; Test 9: restart with no params
(test-num "restart no params"
  77
  (restart-case
    (invoke-restart (quote get-default))
    (get-default () 77)))

;; ============================================================
;; handler-bind Tests
;; ============================================================

(print "")
(print "=== handler-bind Tests ===")

;; Test 10: handler-bind without signal
(test-num "handler-bind no signal"
  60
  (handler-bind
    ((error (lambda (c) 999)))
    (* 6 10)))

;; Test 11: handler-bind with restart invocation
(test-num "handler-bind invokes restart"
  99
  (restart-case
    (handler-bind
      ((error (lambda (c) (invoke-restart (quote use-value) 99))))
      (signal (quote error) "test"))
    (use-value (v) v)))

;; ============================================================
;; find-restart Tests
;; ============================================================

(print "")
(print "=== find-restart Tests ===")

;; Test 12: find-restart for missing restart
(test-nil "find-restart missing"
  (find-restart (quote nonexistent)))

;; Test 13: find-restart for present restart
(test-not-nil "find-restart present"
  (restart-case
    (find-restart (quote use-value))
    (use-value (v) v)))

;; Test 14: find-restart in nested context - inner
(test-not-nil "find-restart inner"
  (restart-case
    (restart-case
      (find-restart (quote inner))
      (inner (v) v))
    (outer (v) v)))

;; Test 15: find-restart in nested context - outer
(test-not-nil "find-restart outer from inner"
  (restart-case
    (restart-case
      (find-restart (quote outer))
      (inner (v) v))
    (outer (v) v)))

;; ============================================================
;; Nested handler-case Tests
;; ============================================================

(print "")
(print "=== Nested Handlers Tests ===")

;; Test 16: nested handler-case - inner catches
(test-num "nested handler-case inner"
  111
  (handler-case
    (handler-case
      (signal (quote error) "test")
      (error (e) 111))
    (error (e) 222)))

;; Test 17: inner handler doesn't match - outer catches
(test-num "nested handler-case outer"
  222
  (handler-case
    (handler-case
      (signal (quote error) "test")
      (type-error (e) 111))
    (error (e) 222)))

;; ============================================================
;; Nested restart-case Tests
;; ============================================================

(print "")
(print "=== Nested Restarts Tests ===")

;; Test 18: nested restart-case - invoke inner
(test-num "nested restart invoke inner"
  10
  (restart-case
    (restart-case
      (invoke-restart (quote inner) 10)
      (inner (v) v))
    (outer (v) (* v 2))))

;; Test 19: invoke outer restart from inner context
(test-num "invoke outer from inner"
  200
  (restart-case
    (restart-case
      (invoke-restart (quote outer) 100)
      (inner (v) v))
    (outer (v) (* v 2))))

;; ============================================================
;; Complex Scenarios
;; ============================================================

(print "")
(print "=== Complex Scenarios ===")

;; Test 20: handler-bind with multiple restarts choice
(test-num "multiple restarts choice"
  42
  (restart-case
    (handler-bind
      ((error (lambda (c) (invoke-restart (quote use-value) 42))))
      (signal (quote error) "choose"))
    (abort () 0)
    (use-value (v) v)
    (retry () 1)))

;; Test 21: conditional restart invocation
(test-num "conditional restart"
  99
  (restart-case
    (if (find-restart (quote special))
        (invoke-restart (quote special) 99)
        0)
    (special (v) v)))

;; Test 22: restart invoked only when present
(test-num "restart present conditional"
  88
  (restart-case
    (if (find-restart (quote maybe))
        (invoke-restart (quote maybe) 88)
        0)
    (maybe (v) v)))

;; ============================================================
;; Error Recovery Patterns
;; ============================================================

(print "")
(print "=== Error Recovery Patterns ===")

;; Test 23: define with recovery restarts
(define (safe-divide x y)
  (restart-case
    (if (= y 0)
        (signal (quote error) "division by zero")
        (/ x y))
    (use-value (v) v)
    (use-zero () 0)))

(test-num "safe-divide normal"
  5
  (safe-divide 10 2))

;; Test 24: recovery with handler - use-value
(test-num "divide recovery via handler"
  999
  (handler-bind
    ((error (lambda (c) (invoke-restart (quote use-value) 999))))
    (safe-divide 10 0)))

;; Test 25: recovery with zero
(test-num "divide recovery use-zero"
  0
  (handler-bind
    ((error (lambda (c) (invoke-restart (quote use-zero)))))
    (safe-divide 10 0)))

;; ============================================================
;; Arithmetic in restart bodies
;; ============================================================

(print "")
(print "=== Arithmetic in Restarts ===")

;; Test 26: restart body with arithmetic
(test-num "restart arithmetic"
  50
  (restart-case
    (invoke-restart (quote compute) 10)
    (compute (n) (* n 5))))

;; Test 27: restart body with multiple operations
(test-num "restart multi-op"
  21
  (restart-case
    (invoke-restart (quote calc) 7)
    (calc (n) (+ (* n 2) n))))

;; ============================================================
;; Handler with condition access
;; ============================================================

(print "")
(print "=== Handler Condition Access ===")

;; Test 28: handler receives condition (just test it doesn't crash)
(test-num "handler receives condition"
  1
  (handler-case
    (signal (quote error) "test-message")
    (error (c) 1)))

;; ============================================================
;; Sequential operations
;; ============================================================

(print "")
(print "=== Sequential Operations ===")

;; Test 29: do in handler body
(test-num "do in handler"
  99
  (handler-case
    (signal (quote error) "boom")
    (error (e)
      (do
        (print "handling...")
        99))))

;; Test 30: do in restart body
(test-num "do in restart"
  88
  (restart-case
    (invoke-restart (quote seq))
    (seq ()
      (do
        (print "restarting...")
        88))))

;; ============================================================
;; Results
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
