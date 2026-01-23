; Test Effect System - Issue 14 P2
; NOTE: Don't use built-in effect names like 'Fail' which is RECOVERY_ABORT mode.
; Use custom effect names for resumable effects.

; REVIEWED:SYNTAX
; Test 1: Handle without effect (body just returns)
(define test1
  (handle
    (+ 1 2)
    (TestFail (payload resume)
      (resume 42))))

; Test 2: Handle with perform - handler should intercept and resume with 42
; Using 'TestFail' instead of 'Fail' because 'Fail' is abort-mode (can't resume)
(define test2
  (handle
    (+ 1 (perform 'TestFail "test error"))
    (TestFail (payload resume)
      (resume 42))))

; Test 3: Simple arithmetic (sanity check)
(define test3 (+ 10 20))

; Test 4: Handle returning without resuming
(define test4
  (handle
    (perform 'MyEffect "data")
    (MyEffect (payload resume)
      100)))

; Test 5: Chained operations after resume
; After resume returns 10, add 5 to it
(define test5
  (handle
    (+ 5 (perform 'Add2 "data"))
    (Add2 (payload resume)
      (resume 10))))

; Test 6: Handler using payload value
(define test6
  (handle
    (perform 'GetValue 5)
    (GetValue (payload resume)
      (resume (* payload 2)))))

; Test 7: Nested arithmetic with effect
(define test7
  (handle
    (* 2 (+ 3 (perform 'Inner 4)))
    (Inner (payload resume)
      (resume (+ payload 1)))))

(println "Test 1 (no effect): " test1)
(println "Test 2 (perform+resume): " test2)
(println "Test 3 (sanity): " test3)
(println "Test 4 (no resume): " test4)
(println "Test 5 (multiple performs): " test5)
(println "Test 6 (use payload): " test6)
(println "Test 7 (nested expr): " test7)
