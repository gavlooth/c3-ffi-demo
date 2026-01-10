;; Test nested fiber resume

(println "Test 1: Simple nested resume")
(define outer (fiber (lambda ()
  (println "Outer: starting")
  (define inner (fiber (lambda ()
    (println "Inner: running")
    42)))
  (define result (resume inner))
  (println "Outer: inner returned")
  (println result)
  (+ result 10))))

(println (resume outer))

(println "Test 2: Nested with-fibers")
(define result2
  (with-fibers
    (define f1 (fiber (lambda ()
      (println "F1: creating F2")
      (define f2 (fiber (lambda ()
        (println "F2: running")
        (* 5 5))))
      (+ (resume f2) 10))))
    (spawn f1)
    (join f1)))
(println result2)

(println "Test 3: Deep nesting")
(define deep
  (fiber (lambda ()
    (define a (fiber (lambda ()
      (define b (fiber (lambda ()
        (define c (fiber (lambda () 1)))
        (+ (resume c) 2))))
      (+ (resume b) 3))))
    (+ (resume a) 4))))
(println (resume deep))

(println "All nested tests complete!")
