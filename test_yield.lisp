;; Test true yield with ucontext-based fibers

; REVIEWED:SYNTAX
(println "Test 1: Basic fiber")
(println (resume (fiber (lambda () (+ 1 2)))))

; REVIEWED:SYNTAX
(println "Test 2: Generator with yield")
(define counter
  (fiber (lambda ()
    (yield 0)
    (yield 1)
    (yield 2)
    3)))

(println "First resume:")
(println (resume counter))
(println "Second resume:")
(println (resume counter))
(println "Third resume:")
(println (resume counter))
(println "Fourth resume (completion):")
(println (resume counter))

(println "Test 3: Yield returns resume value")
(define echo-fiber
  (fiber (lambda ()
    (define received (yield "ready"))
    (yield received)
    "done")))

(println (resume echo-fiber))
(println (resume echo-fiber "hello"))
(println (resume echo-fiber))

(println "All tests complete!")
