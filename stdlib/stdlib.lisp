;; Omni Lisp Standard Library
;; Embedded at compile time via $embed("stdlib/stdlib.lisp")

;; =========================================================================
;; Built-in Effect Declarations (typed I/O effects)
;; =========================================================================
(define [effect] (io/print (^Any x)))
(define [effect] (io/println (^Any x)))
(define [effect] (io/display (^Any x)))
(define [effect] (io/newline (^Any x)))
(define [effect] (io/read-file (^String path)))
(define [effect] (io/write-file (^Any x)))
(define [effect] (io/file-exists? (^String path)))
(define [effect] (io/read-lines (^String path)))
(define [effect] (io/tcp-connect (^Any args)))
(define [effect] (io/tcp-read (^Any handle)))
(define [effect] (io/tcp-write (^Any args)))
(define [effect] (io/tcp-close (^Any handle)))
(define [effect] (io/dns-resolve (^String host)))
(define [effect] (io/async-sleep (^Int ms)))
(define [effect] (io/offload (^Any job)))
(define [effect] (io/thread-spawn (^Any job)))
(define [effect] (io/thread-join (^Int task-id)))
(define [effect] (io/thread-join-timeout (^Any args)))
(define [effect] (io/thread-cancel (^Int task-id)))
(define [effect] (io/tls-connect (^Any args)))
(define [effect] (io/tls-read (^Any handle)))
(define [effect] (io/tls-write (^Any args)))
(define [effect] (io/tls-close (^Any handle)))

;; Error effect — all recoverable errors flow through this
(define [effect] (raise (^Any msg)))

;; =========================================================================
;; Standard Macros (defined before HOFs since macros may be used by them)
;; =========================================================================
(define [macro] when ([test .. body] (if test (begin .. body) nil)))
(define [macro] unless ([test .. body] (if test nil (begin .. body))))
(define [macro] cond ([] nil) ([test body .. rest] (if test body (cond .. rest))))
(define (default v fallback) (if (null? v) fallback v))

;; with-defaults: wrap body with nil-checked defaults for dict destructured bindings
;; Usage: (with-defaults name1 val1 name2 val2 ... body)
;; Last argument is the body expression. Preceding pairs are (name default-value).
;; Expands to nested lets: (let (name1 (default name1 val1)) (let (name2 ...) body))
(define [macro] with-defaults
  ([body] body)
  ([name val .. rest] (let (name (default name val)) (with-defaults .. rest))))
(define with-trampoline (lambda (thunk) (handle (thunk nil) (bounce next-thunk (resolve (with-trampoline next-thunk))))))

;; =========================================================================
;; Type Predicates (defined via is? and abstract type hierarchy)
;; =========================================================================
(define (int? x) (is? x 'Int))
(define (double? x) (is? x 'Double))
(define (number? x) (is? x 'Number))
(define (string? x) (is? x 'String))
(define (symbol? x) (is? x 'Symbol))
(define (boolean? x) (or (null? x) (= x true)))
(define (list? x) (or (null? x) (pair? x)))
(define (closure? x) (is? x 'Closure))
(define (array? x) (is? x 'Array))
(define (dict? x) (is? x 'Dict))

;; =========================================================================
;; Numeric Predicates (defined via dispatched comparison ops)
;; =========================================================================
(define (zero? x) (= x 0))
(define (positive? x) (> x 0))
(define (negative? x) (< x 0))
(define (even? x) (= (% x 2) 0))
(define (odd? x) (not (= (% x 2) 0)))

;; =========================================================================
;; Higher-Order Functions (multi-arg, dispatched on collection type)
;; =========================================================================

;; reverse: (reverse lst) — must be defined before map/filter (they depend on it)
(define (reverse lst) (let loop (xs lst acc nil) (if (null? xs) acc (loop (cdr xs) (cons (car xs) acc)))))

;; map: (map f coll) — apply f to each element, return same collection type
(define (map f (^List lst)) (let loop (xs lst acc nil) (if (null? xs) (reverse acc) (loop (cdr xs) (cons (f (car xs)) acc)))))
(define (map f (^Array arr)) (let (len (length arr) result []) (let loop (i 0) (if (= i len) result (begin (push! result (f (ref arr i))) (loop (+ i 1)))))))
(define (map (^Closure f)) (lambda (coll) (map f coll)))

;; filter: (filter pred coll) — keep elements where (pred x) is truthy
(define (filter pred (^List lst)) (let loop (xs lst acc nil) (if (null? xs) (reverse acc) (if (pred (car xs)) (loop (cdr xs) (cons (car xs) acc)) (loop (cdr xs) acc)))))
(define (filter pred (^Array arr)) (let (len (length arr) result []) (let loop (i 0) (if (= i len) result (if (pred (ref arr i)) (begin (push! result (ref arr i)) (loop (+ i 1))) (loop (+ i 1)))))))
(define (filter (^Closure pred)) (lambda (coll) (filter pred coll)))

;; foldl: (foldl f acc lst) — left fold, f takes 2 args: (f acc x)
(define (foldl f acc lst) (let loop (a acc xs lst) (if (null? xs) a (loop (f a (car xs)) (cdr xs)))))

;; foldr: (foldr f init lst) — right fold, f takes 2 args: (f x acc)
(define (foldr f init lst) (foldl (lambda (acc x) (f x acc)) init (reverse lst)))

;; append: (append a b) — concatenate two lists
(define (append a b) (let loop (xs (reverse a) acc b) (if (null? xs) acc (loop (cdr xs) (cons (car xs) acc)))))

;; compose: (compose f g) — function composition, returns (lambda (x) (f (g x)))
(define (compose f g) (lambda (x) (f (g x))))

;; partial: (partial f . initial-args) — partial application
(define (partial f .. initial-args) (lambda (.. new-args) (apply f (append initial-args new-args))))

;; id: identity function
(define (id x) x)

;; nth: (nth n lst) — get nth element (0-indexed)
(define (nth n lst) (let loop (i n xs lst) (if (= i 0) (car xs) (loop (- i 1) (cdr xs)))))

;; take: (take n lst) — first n elements (iterative)
(define (take n lst) (let loop (i n xs lst acc nil) (if (= i 0) (reverse acc) (if (null? xs) (reverse acc) (loop (- i 1) (cdr xs) (cons (car xs) acc))))))

;; drop: (drop n lst) — skip first n elements
(define (drop n lst) (let loop (i n xs lst) (if (= i 0) xs (if (null? xs) nil (loop (- i 1) (cdr xs))))))

;; zip: (zip a b) — zip two lists into list of pairs (iterative)
(define (zip a b) (let loop (xs a ys b acc nil) (if (or (null? xs) (null? ys)) (reverse acc) (loop (cdr xs) (cdr ys) (cons (cons (car xs) (car ys)) acc)))))

;; range: (range n) — list from 0 to n-1 (iterative, builds in reverse)
(define (range n) (let loop (i (- n 1) acc nil) (if (< i 0) acc (loop (- i 1) (cons i acc)))))

;; for-each: (for-each f lst) — apply f to each element for side effects, return nil
(define (for-each f (^List lst)) (let loop (xs lst) (if (null? xs) nil (begin (f (car xs)) (loop (cdr xs))))))
(define (for-each (^Closure f)) (lambda (lst) (for-each f lst)))

;; any?: (any? pred lst) — true if pred is truthy for any element
(define (any? pred (^List lst)) (let loop (xs lst) (if (null? xs) nil (if (pred (car xs)) true (loop (cdr xs))))))
(define (any? (^Closure pred)) (lambda (lst) (any? pred lst)))

;; every?: (every? pred lst) — true if pred is truthy for all elements
(define (every? pred (^List lst)) (let loop (xs lst) (if (null? xs) true (if (pred (car xs)) (loop (cdr xs)) nil))))
(define (every? (^Closure pred)) (lambda (lst) (every? pred lst)))

;; =========================================================================
;; Error Handling Convention (via algebraic effects)
;; =========================================================================

;; try: run a thunk, catch errors via 'raise' effect
(define (try thunk handler) (handle (thunk nil) (raise msg (handler msg))))

;; assert!: check condition, raise if false
(define (assert! condition msg) (if condition true (signal raise msg)))

;; =========================================================================
;; Association List Helpers
;; =========================================================================

;; assoc: (assoc key alist) — find pair with matching key
(define (assoc key alist) (let loop (xs alist) (if (null? xs) nil (if (= (car (car xs)) key) (car xs) (loop (cdr xs))))))

;; assoc-ref: (assoc-ref key alist) — get value for key (cdr of pair)
(define (assoc-ref key alist) (let (pair (assoc key alist)) (if (null? pair) nil (cdr pair))))

;; =========================================================================
;; Mathematical Constants
;; =========================================================================
(define pi 3.141592653589793)
(define e 2.718281828459045)

;; =========================================================================
;; Additional HOFs
;; =========================================================================

;; flatten: flatten nested lists into a flat list
(define (flatten lst) (let loop (l lst acc nil) (if (null? l) (reverse acc) (if (pair? (car l)) (loop (cdr l) (let loop2 (inner (car l) a acc) (if (null? inner) a (loop2 (cdr inner) (cons (car inner) a))))) (loop (cdr l) (cons (car l) acc))))))

;; partition: split list by predicate, returns (kept . rejected)
(define (partition pred lst) (let loop (l lst yes nil no nil) (if (null? l) (cons (reverse yes) (reverse no)) (if (pred (car l)) (loop (cdr l) (cons (car l) yes) no) (loop (cdr l) yes (cons (car l) no))))))

;; remove: remove elements matching predicate (uses filter)
(define (remove pred lst) (filter (lambda (x) (not (pred x))) lst))

;; find: first element matching predicate, or nil
(define (find pred lst) (let loop (l lst) (if (null? l) nil (if (pred (car l)) (car l) (loop (cdr l))))))

;; =========================================================================
;; Generators & Lazy Streams (using existing effects)
;; =========================================================================

;; stream-yield: (stream-yield val) inside a generator — returns (cons val continuation)
;; Note: yield is now a primitive for fibers. Use stream-yield for generator patterns.
(define [macro] stream-yield ([val] (shift k (cons val k))))

;; stream-take: consume n values from a generator continuation
(define (stream-take n gen) (let loop (i n g gen acc nil) (if (= i 0) (reverse acc) (if (null? g) (reverse acc) (let (pair (if (procedure? g) (g nil) g)) (if (null? pair) (reverse acc) (loop (- i 1) (cdr pair) (cons (car pair) acc))))))))

;; delay/force: promise (lazy evaluation)
(define (delay thunk) (let (result nil forced nil) (lambda () (if forced result (begin (set! result (thunk nil)) (set! forced true) result)))))
(define (force p) (p))

;; =========================================================================
;; Iterators: Lazy Sequences
;; =========================================================================

;; iterator-empty: the empty iterator
(define iterator-empty (make-iterator (lambda () nil)))

;; iterator: convert collection to lazy iterator (dispatched on type)
(define (iterator (^List lst)) (if (null? lst) iterator-empty (make-iterator (lambda () (cons (car lst) (iterator (cdr lst)))))))
(define (iterator (^Array arr)) (let (len (length arr)) (let make-it (i 0) (if (= i len) iterator-empty (make-iterator (lambda () (cons (ref arr i) (make-it (+ i 1)))))))))
(define (iterator (^Dict d)) (iterator (keys d)))
(define (iterator (^Iterator it)) it)

;; Lazy combinators — return new iterators, nothing allocated until consumed
(define (imap f it) (make-iterator (lambda () (let (pair (next it)) (if (null? pair) nil (cons (f (car pair)) (imap f (cdr pair))))))))
(define (ifilter pred it) (make-iterator (lambda () (let loop (pair (next it)) (if (null? pair) nil (if (pred (car pair)) (cons (car pair) (ifilter pred (cdr pair))) (loop (next (cdr pair)))))))))
(define (itake n it) (if (= n 0) iterator-empty (make-iterator (lambda () (let (pair (next it)) (if (null? pair) nil (cons (car pair) (itake (- n 1) (cdr pair)))))))))
(define (idrop n it) (if (= n 0) it (let (pair (next it)) (if (null? pair) iterator-empty (idrop (- n 1) (cdr pair))))))
(define (izip a b) (make-iterator (lambda () (let (pa (next a) pb (next b)) (if (or (null? pa) (null? pb)) nil (cons (cons (car pa) (car pb)) (izip (cdr pa) (cdr pb))))))))

;; Iterator-dispatched map/filter (lazy — delegates to imap/ifilter)
(define (map f (^Iterator it)) (imap f it))
(define (filter pred (^Iterator it)) (ifilter pred it))

;; Infinite sources
(define (range-from n) (make-iterator (lambda () (cons n (range-from (+ n 1))))))
(define (irepeat x) (make-iterator (lambda () (cons x (irepeat x)))))
(define (icycle coll) (let (it (iterator coll)) (make-iterator (lambda () (let (pair (next it)) (if (null? pair) (next (icycle coll)) pair))))))

;; ifoldl: eagerly fold an iterator
(define (ifoldl f acc it) (let loop (a acc cur it) (let (pair (next cur)) (if (null? pair) a (loop (f a (car pair)) (cdr pair))))))

;; =========================================================================
;; I/O Effect Wrappers
;; These redefine I/O operations to go through the effects system.
;; When no handler is installed, the fast path calls __raw-* directly.
;; =========================================================================
(define print (lambda (x) (signal io/print x)))
(define println (lambda (x) (signal io/println x)))
(define display (lambda (x) (signal io/display x)))
(define newline (lambda () (signal io/newline nil)))
(define read-file (lambda (path) (signal io/read-file path)))
(define write-file (lambda (path content) (signal io/write-file (cons path content))))
(define file-exists? (lambda (path) (signal io/file-exists? path)))
(define read-lines (lambda (path) (signal io/read-lines path)))
(define tcp-connect (lambda (host port) (signal io/tcp-connect (cons host port))))
(define tcp-read (lambda (handle) (signal io/tcp-read handle)))
(define tcp-write (lambda (handle data) (signal io/tcp-write (cons handle data))))
(define tcp-close (lambda (handle) (signal io/tcp-close handle)))
(define dns-resolve (lambda (host) (signal io/dns-resolve host)))
(define async-sleep (lambda (ms) (signal io/async-sleep ms)))
(define offload (lambda (op .. args) (signal io/offload (cons op args))))
(define thread-spawn (lambda (op .. args) (signal io/thread-spawn (cons op args))))
(define thread-join (lambda (task-id) (signal io/thread-join task-id)))
(define thread-join-timeout (lambda (task-id timeout-ms) (signal io/thread-join-timeout (cons task-id timeout-ms))))
(define thread-cancel (lambda (task-id) (signal io/thread-cancel task-id)))
(define tls-connect (lambda (tcp-handle hostname) (signal io/tls-connect (cons tcp-handle hostname))))
(define tls-read (lambda (handle) (signal io/tls-read handle)))
(define tls-write (lambda (handle data) (signal io/tls-write (cons handle data))))
(define tls-close (lambda (handle) (signal io/tls-close handle)))
(define [effect] (io/http-get (^String url)))
(define [effect] (io/http-request (^Any args)))
(define http-get (lambda (url) (signal io/http-get url)))

;; =========================================================================
;; Handler Composition
;; =========================================================================
;; Each handler is a function (thunk -> result) that wraps a thunk in handle.
;; with-handlers chains and runs them: outer handlers wrap inner handlers.
(define (with-handlers handlers thunk) (if (null? handlers) (thunk) ((car handlers) (lambda () (with-handlers (cdr handlers) thunk)))))
