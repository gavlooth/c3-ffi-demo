module lisp;


import std::io;
import main;
fn void run_escape_scope_core_tests(Interp* interp, int* pass, int* fail) {
    test_eq(interp, "escape-scope: reverse '(1 2 3) car => 3",
        "(car (reverse (quote (1 2 3))))", 3, pass, fail);
    test_eq(interp, "escape-scope: reverse length => 3",
        "(length (reverse (quote (1 2 3))))", 3, pass, fail);

    test_eq(interp, "escape-scope: map car => 2",
        "(car (map (lambda (x) (* x 2)) (quote (1 2 3))))", 2, pass, fail);
    test_eq(interp, "escape-scope: map length => 3",
        "(length (map (lambda (x) (+ x 1)) (quote (1 2 3))))", 3, pass, fail);

    test_eq(interp, "escape-scope: filter car => 2",
        "(car (filter (lambda (x) (> x 1)) (quote (1 2 3))))", 2, pass, fail);
    test_eq(interp, "escape-scope: filter length => 2",
        "(length (filter (lambda (x) (> x 1)) (quote (1 2 3))))", 2, pass, fail);

    test_eq(interp, "escape-scope: foldl non-list result",
        "(foldl + 0 (quote (1 2 3 4 5)))", 15, pass, fail);

    test_eq(interp, "escape-scope: nested map+reverse",
        "(car (reverse (map (lambda (x) (* x 10)) (quote (1 2 3)))))", 30, pass, fail);
    test_eq(interp, "escape-scope: captured env map+reverse",
        "(let (m 10) (car (reverse (map (lambda (x) (* x m)) (quote (1 2 3))))))", 30, pass, fail);

    test_eq(interp, "escape-scope: handle + map",
        "(handle (car (map (lambda (x) (+ x 1)) (quote (10 20 30)))) (tag arg (resolve 0)))", 11, pass, fail);
}

fn void run_escape_scope_iterator_coroutine_tests(Interp* interp, int* pass, int* fail) {
    setup(interp, "(define esc-consume (lambda (it) (let loop (acc nil val (next it)) (if (null? val) (reverse acc) (loop (cons (car val) acc) (next (cdr val)))))))");
    test_eq(interp, "escape-scope: iterator consume car",
        "(car (esc-consume (iterator (quote (1 2 3)))))", 1, pass, fail);
    test_eq(interp, "escape-scope: iterator consume len",
        "(length (esc-consume (iterator (quote (1 2 3)))))", 3, pass, fail);

    test_eq(interp, "escape-scope: coroutine yield",
        "(let (g (coroutine (lambda () (yield 42) 99))) (resume g))", 42, pass, fail);
    test_eq(interp, "escape-scope: coroutine yield+complete",
        "(let (g (coroutine (lambda () (yield 42) 99))) (resume g) (resume g))", 99, pass, fail);
}

fn void run_escape_scope_env_lane_tests(Interp* interp, int* pass, int* fail) {
    setup(interp, "(define big-list (range 1000))");
    test_eq(interp, "escape-scope: reverse 1000 car => 999",
        "(car (reverse big-list))", 999, pass, fail);
    test_eq(interp, "escape-scope: map 1000 length",
        "(length (map (lambda (x) (+ x 1)) big-list))", 1000, pass, fail);

    setup(interp, "(define my-rev (lambda (lst) (let loop (xs lst acc (quote ())) (if (null? xs) acc (loop (cdr xs) (cons (car xs) acc))))))");
    test_eq(interp, "esc-env: reverse car",
        "(car (my-rev (quote (1 2 3))))", 3, pass, fail);
    test_eq(interp, "esc-env: reverse length",
        "(length (my-rev (quote (1 2 3))))", 3, pass, fail);
    test_nil(interp, "esc-env: reverse empty",
        "(my-rev (quote ()))", pass, fail);
    test_eq(interp, "esc-env: reverse singleton",
        "(car (my-rev (quote (42))))", 42, pass, fail);

    test_eq(interp, "esc-env: reverse 10000 length",
        "(length (my-rev (range 10000)))", 10000, pass, fail);

    setup(interp, "(define my-map (lambda (f lst) (let loop (xs lst acc (quote ())) (if (null? xs) (reverse acc) (loop (cdr xs) (cons (f (car xs)) acc))))))");
    test_eq(interp, "esc-env: map double",
        "(car (my-map (lambda (x) (* x 2)) (quote (1 2 3))))", 2, pass, fail);
    test_eq(interp, "esc-env: map 10000 length",
        "(length (my-map (lambda (x) (+ x 1)) (range 10000)))", 10000, pass, fail);

    setup(interp, "(define my-filter (lambda (p lst) (let loop (xs lst acc (quote ())) (if (null? xs) (reverse acc) (if (p (car xs)) (loop (cdr xs) (cons (car xs) acc)) (loop (cdr xs) acc))))))");
    test_eq(interp, "esc-env: filter gt2",
        "(length (my-filter (lambda (x) (> x 2)) (quote (1 2 3 4 5))))", 3, pass, fail);

    test_eq(interp, "esc-env: sum 100",
        "(let loop (n 0 sum 0) (if (= n 100) sum (loop (+ n 1) (+ sum n))))", 4950, pass, fail);

    setup(interp, "(define flatten (lambda (lst) (let outer (xs lst acc (quote ())) (if (null? xs) (reverse acc) (if (list? (car xs)) (let inner (ys (car xs) acc2 acc) (if (null? ys) (outer (cdr xs) acc2) (inner (cdr ys) (cons (car ys) acc2)))) (outer (cdr xs) (cons (car xs) acc)))))))");
    test_eq(interp, "esc-env: flatten nested",
        "(length (flatten (quote ((1 2) (3 4) (5)))))", 5, pass, fail);
    test_eq(interp, "esc-env: flatten car",
        "(car (flatten (quote ((1 2) (3 4) (5)))))", 1, pass, fail);

    test_eq(interp, "esc-env: handle+loop",
        "(handle (let loop (n 0 sum 0) (if (= n 5) sum (loop (+ n 1) (+ sum (signal get n))))) (get arg (resolve (* arg 2))))", 20, pass, fail);
    test_eq(interp, "esc-env: shift-in-loop fallback",
        "(reset (let loop (n 0 acc 0) (if (= n 3) acc (loop (+ n 1) (+ acc (shift k (k 10)))))))", 30, pass, fail);
}

fn void run_escape_scope_tests(Interp* interp, int* pass, int* fail) {
    io::printn("\n--- Escape-Scope Optimization Tests ---");
    run_escape_scope_core_tests(interp, pass, fail);
    run_escape_scope_iterator_coroutine_tests(interp, pass, fail);
    run_escape_scope_env_lane_tests(interp, pass, fail);
}
