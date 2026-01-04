;; Test with-fibers scoped execution

(println "Test 1: Basic with-fibers")
(define result
  (with-fibers
    (define f1 (fiber (lambda () (+ 10 20))))
    (spawn f1)
    "body-result"))
(println result)

(println "Test 2: Multiple fibers completing")
(define counter 0)
(with-fibers
  (define f1 (fiber (lambda ()
    (set! counter (+ counter 1))
    (yield)
    (set! counter (+ counter 10)))))
  (define f2 (fiber (lambda ()
    (set! counter (+ counter 100))
    (yield)
    (set! counter (+ counter 1000)))))
  (spawn f1)
  (spawn f2))
(println "Counter after both fibers complete:")
(println counter)

(println "Test 3: Fibers with channels")
(define ch-result
  (with-fibers
    (define ch (chan 3))  ;; buffered channel
    (define producer (fiber (lambda ()
      (send ch 1)
      (send ch 2)
      (send ch 3)
      "sent")))
    (define consumer (fiber (lambda ()
      (define v1 (recv ch))
      (define v2 (recv ch))
      (define v3 (recv ch))
      (+ v1 v2 v3))))
    (spawn producer)
    (spawn consumer)
    (join consumer)))
(println ch-result)

(println "Test 4: Join returns fiber result")
(define join-result
  (with-fibers
    (define f (fiber (lambda () (* 7 8))))
    (spawn f)
    (join f)))
(println join-result)

;; Note: Nested with-fibers (fiber resuming fiber) is not supported
;; due to single global fiber_resumer_ctx limitation

(println "All with-fibers tests complete!")
