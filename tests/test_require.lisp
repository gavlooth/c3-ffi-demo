;; test_require.lisp - Tests for module auto-loading via require
;;
;; Tests that require can find, compile, and load modules from disk.
;;
;; Run with: ./omni tests/test_require.lisp

;; ============================================================
;; Test Framework
;; ============================================================

(define test-count 0)
(define pass-count 0)
(define fail-count 0)

(define test-not-nothing [name] [value]
  (set! test-count (+ test-count 1))
  (if (nothing? value)
      (do
        (set! fail-count (+ fail-count 1))
        (print "FAIL:" name "(got nothing)"))
      (do
        (set! pass-count (+ pass-count 1))
        (print "PASS:" name))))

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
;; Require Tests
;; ============================================================

(print "")
(print "=== Require Auto-Loading Tests ===")

;; Test 1: Require a module from tests/lib directory
;; Note: Need to set OMNI_MODULE_PATH or be in the right directory
(print "")
(print "Test 1: Require math_helpers module")
(define math-mod (require 'math_helpers))
(test-not-nothing "require returns module" math-mod)

;; Test 2: Module should be registered after require
(print "")
(print "Test 2: Module registered after require")
(test-not-nothing "module-get finds required module"
  (module-get 'math_helpers))

;; Test 3: Access exported values via module-ref
(print "")
(print "Test 3: Access exports from required module")
(test-not-nothing "module-ref gets pi"
  (module-ref 'math_helpers 'pi))

;; Test 4: Access exported function
(print "")
(print "Test 4: Access exported function")
(define double-fn (module-ref 'math_helpers 'double))
(test-not-nothing "module-ref gets double function" double-fn)

;; Test 5: Call exported function (if we got it)
(print "")
(print "Test 5: Call exported function")
(if (not (nothing? double-fn))
    (test-equal "double 5 = 10" 10 (double-fn 5))
    (do
      (set! test-count (+ test-count 1))
      (set! fail-count (+ fail-count 1))
      (print "FAIL: could not test double function (not loaded)")))

;; Test 6: Require same module again should be fast (cached)
(print "")
(print "Test 6: Require same module again (should be cached)")
(define math-mod-2 (require 'math_helpers))
(test-not-nothing "require cached module" math-mod-2)

;; Test 7: Require non-existent module
(print "")
(print "Test 7: Require non-existent module")
(define bad-mod (require 'nonexistent_module_xyz))
(if (nothing? bad-mod)
    (do
      (set! test-count (+ test-count 1))
      (set! pass-count (+ pass-count 1))
      (print "PASS: require non-existent returns nothing"))
    (do
      (set! test-count (+ test-count 1))
      (set! fail-count (+ fail-count 1))
      (print "FAIL: require non-existent should return nothing")))

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

fail-count
